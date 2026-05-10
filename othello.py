from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable


EMPTY = "."
BLACK = "X"
WHITE = "O"
PLAYERS = {BLACK, WHITE}

Move = tuple[int, int]

DIRECTIONS: tuple[Move, ...] = (
    (-1, -1),
    (-1, 0),
    (-1, 1),
    (0, -1),
    (0, 1),
    (1, -1),
    (1, 0),
    (1, 1),
)


@dataclass(frozen=True)
class Board:
    cells: tuple[tuple[str, ...], ...]

    def __post_init__(self) -> None:
        size = len(self.cells)
        if size not in (6, 8):
            raise ValueError("board size must be 6x6 or 8x8")
        if any(len(row) != size for row in self.cells):
            raise ValueError("board must be square")
        invalid = sorted({cell for row in self.cells for cell in row} - {EMPTY, BLACK, WHITE})
        if invalid:
            raise ValueError(f"invalid board characters: {''.join(invalid)}")

    @property
    def size(self) -> int:
        return len(self.cells)

    def inside(self, row: int, col: int) -> bool:
        return 0 <= row < self.size and 0 <= col < self.size

    def get(self, row: int, col: int) -> str:
        return self.cells[row][col]

    def count(self, player: str) -> int:
        return sum(cell == player for row in self.cells for cell in row)

    def empty_count(self) -> int:
        return sum(cell == EMPTY for row in self.cells for cell in row)

    def is_full(self) -> bool:
        return self.empty_count() == 0

    def with_move(self, player: str, move: Move) -> "Board":
        flips = self.flips_for_move(player, move)
        if not flips:
            raise ValueError(f"illegal move {format_move(move)} for {player}")

        mutable = [list(row) for row in self.cells]
        row, col = move
        mutable[row][col] = player
        for r, c in flips:
            mutable[r][c] = player
        return Board(tuple(tuple(row) for row in mutable))

    def flips_for_move(self, player: str, move: Move) -> list[Move]:
        validate_player(player)
        row, col = move
        if not self.inside(row, col) or self.get(row, col) != EMPTY:
            return []

        opponent = other_player(player)
        flips: list[Move] = []
        for dr, dc in DIRECTIONS:
            path: list[Move] = []
            r, c = row + dr, col + dc
            while self.inside(r, c) and self.get(r, c) == opponent:
                path.append((r, c))
                r += dr
                c += dc
            if path and self.inside(r, c) and self.get(r, c) == player:
                flips.extend(path)
        return flips

    def legal_moves(self, player: str) -> list[Move]:
        validate_player(player)
        moves = [
            (row, col)
            for row in range(self.size)
            for col in range(self.size)
            if self.flips_for_move(player, (row, col))
        ]
        return sorted(moves)

    def game_over(self) -> bool:
        return self.is_full() or (
            not self.legal_moves(BLACK) and not self.legal_moves(WHITE)
        )

    def to_text(self) -> str:
        return "\n".join("".join(row) for row in self.cells)


def validate_player(player: str) -> None:
    if player not in PLAYERS:
        raise ValueError("player must be X or O")


def other_player(player: str) -> str:
    validate_player(player)
    return WHITE if player == BLACK else BLACK


def parse_position(text: str) -> tuple[Board, str]:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if not lines:
        raise ValueError("input is empty")

    turn_line = lines[-1]
    if not turn_line.lower().startswith("turn:"):
        raise ValueError('last line must be "turn: X" or "turn: O"')
    turn = turn_line.split(":", 1)[1].strip().upper()
    validate_player(turn)

    board_lines = lines[:-1]
    size = len(board_lines)
    if size not in (6, 8):
        raise ValueError("board must have 6 or 8 rows")
    if any(len(line) != size for line in board_lines):
        raise ValueError("board must be 6x6 or 8x8")

    board = Board(tuple(tuple(line.upper()) for line in board_lines))
    return board, turn


def format_move(move: Move) -> str:
    row, col = move
    return f"{chr(ord('a') + col)}{row + 1}"


def parse_move(value: str) -> Move:
    value = value.strip().lower()
    if len(value) < 2 or not value[0].isalpha() or not value[1:].isdigit():
        raise ValueError("move must look like d3")
    col = ord(value[0]) - ord("a")
    row = int(value[1:]) - 1
    return row, col


def board_after_moves(board: Board, player: str) -> Iterable[tuple[Move, Board]]:
    for move in board.legal_moves(player):
        yield move, board.with_move(player, move)
