#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int N = 6;
constexpr int CELLS = N * N;
constexpr uint64_t FULL = (1ULL << CELLS) - 1ULL;
constexpr int MAX_MOVES = CELLS - 4;
constexpr int MAX_LEGAL_MOVES = CELLS;
constexpr uint64_t EXPECTED_TOTAL = 81'600'000'000ULL;

struct Key {
    uint64_t x;
    uint64_t o;
    bool x_turn;

    bool operator==(const Key& other) const {
        return x == other.x && o == other.o && x_turn == other.x_turn;
    }
};

struct KeyHash {
    size_t operator()(const Key& key) const {
        uint64_t h = key.x;
        h ^= key.o + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= static_cast<uint64_t>(key.x_turn) + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

struct Move {
    uint64_t square;
    uint64_t flips;
};

struct MoveList {
    std::array<Move, MAX_LEGAL_MOVES> moves;
    int size = 0;
};

struct Position {
    uint64_t x = 0;
    uint64_t o = 0;
    bool x_turn = true;
};

struct StopSearch {};

uint64_t not_left_col = 0;
uint64_t not_right_col = 0;
uint64_t not_top_row = 0;
uint64_t not_bottom_row = 0;
bool has_deadline = false;
std::chrono::steady_clock::time_point deadline;

std::unordered_map<Key, uint64_t, KeyHash> memo;
std::atomic<bool> done{false};
std::atomic<uint64_t> calls{0};
std::atomic<uint64_t> cache_hits{0};
std::atomic<uint64_t> generated_moves{0};
std::atomic<uint64_t> memo_entries{0};
std::atomic<int> current_move_number{0};
std::atomic<int> deepest_move_number{0};
std::array<uint64_t, MAX_MOVES + 1> states_by_move;
uint64_t local_calls = 0;
uint64_t local_cache_hits = 0;
uint64_t local_generated_moves = 0;
int local_current_move_number = 0;
int local_deepest_move_number = 0;

void flush_progress_counters() {
    if (local_calls != 0) {
        calls.fetch_add(local_calls, std::memory_order_relaxed);
        local_calls = 0;
    }
    if (local_cache_hits != 0) {
        cache_hits.fetch_add(local_cache_hits, std::memory_order_relaxed);
        local_cache_hits = 0;
    }
    if (local_generated_moves != 0) {
        generated_moves.fetch_add(local_generated_moves, std::memory_order_relaxed);
        local_generated_moves = 0;
    }
    current_move_number.store(local_current_move_number, std::memory_order_relaxed);

    int previous = deepest_move_number.load(std::memory_order_relaxed);
    while (local_deepest_move_number > previous &&
           !deepest_move_number.compare_exchange_weak(previous, local_deepest_move_number, std::memory_order_relaxed)) {
    }
}

void stop_if_deadline_expired() {
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
        flush_progress_counters();
        throw StopSearch{};
    }
}

std::string format_duration(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const uint64_t total = static_cast<uint64_t>(seconds);
    const uint64_t days = total / 86400;
    const uint64_t hours = (total % 86400) / 3600;
    const uint64_t minutes = (total % 3600) / 60;
    const uint64_t secs = total % 60;
    return std::to_string(days) + "日"
        + std::to_string(hours) + "時間"
        + std::to_string(minutes) + "分"
        + std::to_string(secs) + "秒";
}

uint64_t bit(int row, int col) {
    return 1ULL << (row * N + col);
}

void init_masks() {
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            const uint64_t square = bit(row, col);
            if (col != 0) {
                not_left_col |= square;
            }
            if (col != N - 1) {
                not_right_col |= square;
            }
            if (row != 0) {
                not_top_row |= square;
            }
            if (row != N - 1) {
                not_bottom_row |= square;
            }
        }
    }
}

bool inside(int row, int col) {
    return 0 <= row && row < N && 0 <= col && col < N;
}

int popcount(uint64_t value) {
    return __builtin_popcountll(value);
}

std::vector<std::string> read_non_empty_lines(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + path);
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

Position parse_position(const std::string& path) {
    const auto lines = read_non_empty_lines(path);
    if (lines.size() != N + 1) {
        throw std::runtime_error("this counter expects a 6x6 board plus a turn line");
    }

    Position pos;
    for (int row = 0; row < N; ++row) {
        if (static_cast<int>(lines[row].size()) != N) {
            throw std::runtime_error("board rows must have length 6");
        }
        for (int col = 0; col < N; ++col) {
            const char c = lines[row][col];
            if (c == 'X' || c == 'x') {
                pos.x |= bit(row, col);
            } else if (c == 'O' || c == 'o') {
                pos.o |= bit(row, col);
            } else if (c != '.') {
                throw std::runtime_error("board contains an invalid character");
            }
        }
    }

    const std::string turn_line = lines.back();
    if (turn_line.find("turn:") != 0 && turn_line.find("TURN:") != 0) {
        throw std::runtime_error("last line must be turn: X or turn: O");
    }
    const char turn = turn_line.back();
    if (turn == 'X' || turn == 'x') {
        pos.x_turn = true;
    } else if (turn == 'O' || turn == 'o') {
        pos.x_turn = false;
    } else {
        throw std::runtime_error("turn must be X or O");
    }
    return pos;
}

uint64_t shift_n(uint64_t value) {
    return (value & not_top_row) >> N;
}

uint64_t shift_s(uint64_t value) {
    return (value & not_bottom_row) << N;
}

uint64_t shift_w(uint64_t value) {
    return (value & not_left_col) >> 1;
}

uint64_t shift_e(uint64_t value) {
    return (value & not_right_col) << 1;
}

uint64_t shift_nw(uint64_t value) {
    return (value & not_top_row & not_left_col) >> (N + 1);
}

uint64_t shift_ne(uint64_t value) {
    return (value & not_top_row & not_right_col) >> (N - 1);
}

uint64_t shift_sw(uint64_t value) {
    return (value & not_bottom_row & not_left_col) << (N - 1);
}

uint64_t shift_se(uint64_t value) {
    return (value & not_bottom_row & not_right_col) << (N + 1);
}

template <uint64_t (*Shift)(uint64_t)>
uint64_t legal_in_direction(uint64_t own, uint64_t opp, uint64_t empty) {
    uint64_t candidates = Shift(own) & opp;
    uint64_t captured = candidates;
    for (int i = 0; i < N - 2; ++i) {
        candidates = Shift(candidates) & opp;
        captured |= candidates;
    }
    return Shift(captured) & empty;
}

uint64_t legal_move_bits(uint64_t own, uint64_t opp) {
    const uint64_t empty = FULL ^ (own | opp);
    return legal_in_direction<shift_n>(own, opp, empty)
        | legal_in_direction<shift_s>(own, opp, empty)
        | legal_in_direction<shift_w>(own, opp, empty)
        | legal_in_direction<shift_e>(own, opp, empty)
        | legal_in_direction<shift_nw>(own, opp, empty)
        | legal_in_direction<shift_ne>(own, opp, empty)
        | legal_in_direction<shift_sw>(own, opp, empty)
        | legal_in_direction<shift_se>(own, opp, empty);
}

template <uint64_t (*Shift)(uint64_t)>
uint64_t flips_in_direction(uint64_t own, uint64_t opp, uint64_t square) {
    uint64_t cursor = Shift(square);
    uint64_t flips = 0;
    while (cursor && (cursor & opp)) {
        flips |= cursor;
        cursor = Shift(cursor);
    }
    return (cursor & own) ? flips : 0;
}

uint64_t flips_for_square(uint64_t own, uint64_t opp, uint64_t square) {
    return flips_in_direction<shift_n>(own, opp, square)
        | flips_in_direction<shift_s>(own, opp, square)
        | flips_in_direction<shift_w>(own, opp, square)
        | flips_in_direction<shift_e>(own, opp, square)
        | flips_in_direction<shift_nw>(own, opp, square)
        | flips_in_direction<shift_ne>(own, opp, square)
        | flips_in_direction<shift_sw>(own, opp, square)
        | flips_in_direction<shift_se>(own, opp, square);
}

MoveList legal_moves(uint64_t own, uint64_t opp) {
    MoveList list;
    uint64_t bits = legal_move_bits(own, opp);
    while (bits) {
        const uint64_t square = bits & (~bits + 1ULL);
        const uint64_t flips = flips_for_square(own, opp, square);
        if (flips) {
            list.moves[list.size++] = {square, flips};
        }
        bits ^= square;
    }
    local_generated_moves += static_cast<uint64_t>(list.size);
    return list;
}

void note_move_number(uint64_t x, uint64_t o) {
    const int move_number = popcount(x | o) - 4;
    local_current_move_number = move_number;
    local_deepest_move_number = std::max(local_deepest_move_number, move_number);
    if (0 <= move_number && move_number <= MAX_MOVES) {
        ++states_by_move[move_number];
    }
}

uint64_t count_games(uint64_t x, uint64_t o, bool x_turn) {
    ++local_calls;
    if ((local_calls & 0xfffULL) == 0) {
        flush_progress_counters();
        stop_if_deadline_expired();
    }
    note_move_number(x, o);

    const Key key{x, o, x_turn};
    const auto found = memo.find(key);
    if (found != memo.end()) {
        ++local_cache_hits;
        return found->second;
    }

    const uint64_t own = x_turn ? x : o;
    const uint64_t opp = x_turn ? o : x;
    const auto moves = legal_moves(own, opp);
    if (moves.size == 0) {
        const auto opponent_moves = legal_moves(opp, own);
        if (opponent_moves.size == 0) {
            memo.emplace(key, 1);
            memo_entries.fetch_add(1, std::memory_order_relaxed);
            return 1;
        }
        const uint64_t result = count_games(x, o, !x_turn);
        memo.emplace(key, result);
        memo_entries.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    uint64_t total = 0;
    for (int i = 0; i < moves.size; ++i) {
        const auto& move = moves.moves[i];
        if (x_turn) {
            total += count_games(x | move.square | move.flips, o & ~move.flips, false);
        } else {
            total += count_games(x & ~move.flips, o | move.square | move.flips, true);
        }
    }
    memo.emplace(key, total);
    memo_entries.fetch_add(1, std::memory_order_relaxed);
    return total;
}

void print_progress(std::chrono::steady_clock::time_point start) {
    using namespace std::chrono_literals;
    while (!done.load(std::memory_order_relaxed)) {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(1s);
            if (done.load(std::memory_order_relaxed)) {
                return;
            }
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto call_count = calls.load(std::memory_order_relaxed);
        const auto hit_count = cache_hits.load(std::memory_order_relaxed);
        const int current = current_move_number.load(std::memory_order_relaxed);
        const int deepest = deepest_move_number.load(std::memory_order_relaxed);
        const double rate = elapsed > 0.0 ? call_count / elapsed : 0.0;
        const double per_minute = rate * 60.0;
        const double per_hour = rate * 3600.0;
        const double per_day = rate * 86400.0;
        const uint64_t remaining_calls = call_count < EXPECTED_TOTAL ? EXPECTED_TOTAL - call_count : 0;
        const double remaining_seconds = rate > 0.0 ? remaining_calls / rate : 0.0;
        const double percent = 100.0 * static_cast<double>(call_count) / static_cast<double>(EXPECTED_TOTAL);

        std::cerr
            << "[progress] "
            << "elapsed=" << static_cast<uint64_t>(elapsed) << "s "
            << "current=" << current << "手目 "
            << "deepest=" << deepest << "手目 "
            << "calls=" << call_count << " "
            << "expected=" << EXPECTED_TOTAL << " "
            << "done=" << percent << "% "
            << "cache_hits=" << hit_count << " "
            << "memo=" << memo_entries.load(std::memory_order_relaxed) << " "
            << "rate=" << static_cast<uint64_t>(rate) << "手/秒 "
            << "per_min=" << static_cast<uint64_t>(per_minute) << "手/分 "
            << "per_hour=" << static_cast<uint64_t>(per_hour) << "手/時 "
            << "per_day=" << static_cast<uint64_t>(per_day) << "手/日 "
            << "remaining=" << format_duration(remaining_seconds)
            << std::endl;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        init_masks();
        std::string input_path = "examples/start6.txt";
        double max_seconds = 0.0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--max-seconds") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("--max-seconds requires a value");
                }
                max_seconds = std::stod(argv[++i]);
            } else {
                input_path = arg;
            }
        }
        for (auto& value : states_by_move) {
            value = 0;
        }

        const Position pos = parse_position(input_path);
        memo.reserve(8'000'000);

        const auto start = std::chrono::steady_clock::now();
        if (max_seconds > 0.0) {
            has_deadline = true;
            deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(max_seconds));
        }
        std::thread reporter(print_progress, start);

        uint64_t result = 0;
        bool completed = false;
        try {
            result = count_games(pos.x, pos.o, pos.x_turn);
            completed = true;
        } catch (const StopSearch&) {
            flush_progress_counters();
        }
        flush_progress_counters();
        done.store(true, std::memory_order_relaxed);
        reporter.join();

        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "completed=" << (completed ? "true" : "false") << '\n';
        if (completed) {
            std::cout << "total_games=" << result << '\n';
        }
        std::cout << "elapsed_sec=" << elapsed << '\n';
        std::cout << "calls=" << calls.load() << '\n';
        const double final_rate = elapsed > 0.0 ? calls.load() / elapsed : 0.0;
        const uint64_t final_remaining = calls.load() < EXPECTED_TOTAL ? EXPECTED_TOTAL - calls.load() : 0;
        std::cout << "expected_total=" << EXPECTED_TOTAL << '\n';
        std::cout << "rate_per_sec=" << static_cast<uint64_t>(final_rate) << '\n';
        std::cout << "rate_per_min=" << static_cast<uint64_t>(final_rate * 60.0) << '\n';
        std::cout << "rate_per_hour=" << static_cast<uint64_t>(final_rate * 3600.0) << '\n';
        std::cout << "rate_per_day=" << static_cast<uint64_t>(final_rate * 86400.0) << '\n';
        std::cout << "estimated_remaining=" << format_duration(final_rate > 0.0 ? final_remaining / final_rate : 0.0) << '\n';
        std::cout << "cache_hits=" << cache_hits.load() << '\n';
        std::cout << "generated_moves=" << generated_moves.load() << '\n';
        std::cout << "memo_states=" << memo.size() << '\n';
        std::cout << "states_by_move:" << '\n';
        for (int i = 0; i <= MAX_MOVES; ++i) {
            const auto count = states_by_move[i];
            if (count != 0) {
                std::cout << "  " << i << "手目: " << count << '\n';
            }
        }
    } catch (const std::exception& exc) {
        std::cerr << "error: " << exc.what() << std::endl;
        return 1;
    }
    return 0;
}
