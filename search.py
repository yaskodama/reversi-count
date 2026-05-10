from __future__ import annotations

from dataclasses import dataclass

from othello import BLACK, WHITE, Board, Move, other_player


@dataclass(frozen=True)
class SearchResult:
    move: Move | None
    score: int
    nodes: int
    depth: int


INF = 10**12


def choose_depth(board: Board) -> int:
    empties = board.empty_count()
    if empties <= 10:
        return empties
    if board.size == 6:
        return 7
    return 5


def find_best_move(board: Board, player: str, depth: int | None = None) -> SearchResult:
    depth = choose_depth(board) if depth is None else depth
    moves = board.legal_moves(player)
    if not moves:
        return SearchResult(None, evaluate(board, player), 1, depth)

    best_move: Move | None = None
    best_score = -INF
    nodes = 0
    alpha = -INF

    for move in ordered_moves(board, player, moves):
        child = board.with_move(player, move)
        score, child_nodes = alphabeta(
            child,
            other_player(player),
            depth - 1,
            alpha,
            INF,
            player,
        )
        nodes += child_nodes + 1
        if score > best_score:
            best_score = score
            best_move = move
        alpha = max(alpha, best_score)

    return SearchResult(best_move, int(best_score), nodes, depth)


def alphabeta(
    board: Board,
    turn: str,
    depth: int,
    alpha: int | float,
    beta: int | float,
    maximizing_player: str,
) -> tuple[int, int]:
    if depth <= 0 or board.game_over():
        return evaluate(board, maximizing_player), 1

    moves = board.legal_moves(turn)
    if not moves:
        opponent = other_player(turn)
        if not board.legal_moves(opponent):
            return evaluate(board, maximizing_player), 1
        score, nodes = alphabeta(board, opponent, depth - 1, alpha, beta, maximizing_player)
        return score, nodes + 1

    nodes = 1
    if turn == maximizing_player:
        value = -INF
        for move in ordered_moves(board, turn, moves):
            child = board.with_move(turn, move)
            score, child_nodes = alphabeta(
                child,
                other_player(turn),
                depth - 1,
                alpha,
                beta,
                maximizing_player,
            )
            nodes += child_nodes
            value = max(value, score)
            alpha = max(alpha, value)
            if alpha >= beta:
                break
        return int(value), nodes

    value = INF
    for move in ordered_moves(board, turn, moves):
        child = board.with_move(turn, move)
        score, child_nodes = alphabeta(
            child,
            other_player(turn),
            depth - 1,
            alpha,
            beta,
            maximizing_player,
        )
        nodes += child_nodes
        value = min(value, score)
        beta = min(beta, value)
        if alpha >= beta:
            break
    return int(value), nodes


def evaluate(board: Board, player: str) -> int:
    opponent = other_player(player)
    if board.game_over():
        diff = board.count(player) - board.count(opponent)
        if diff > 0:
            return 1_000_000 + diff
        if diff < 0:
            return -1_000_000 + diff
        return 0

    piece_score = 10 * (board.count(player) - board.count(opponent))
    corner_score = 250 * (corner_count(board, player) - corner_count(board, opponent))
    mobility_score = 40 * (
        len(board.legal_moves(player)) - len(board.legal_moves(opponent))
    )
    stability_score = 80 * (
        edge_stability_like_count(board, player)
        - edge_stability_like_count(board, opponent)
    )
    danger_score = -120 * (
        corner_adjacent_empty_count(board, player)
        - corner_adjacent_empty_count(board, opponent)
    )
    return piece_score + corner_score + mobility_score + stability_score + danger_score


def ordered_moves(board: Board, player: str, moves: list[Move]) -> list[Move]:
    return sorted(
        moves,
        key=lambda move: move_priority(board, player, move),
        reverse=True,
    )


def move_priority(board: Board, player: str, move: Move) -> int:
    row, col = move
    last = board.size - 1
    score = len(board.flips_for_move(player, move))
    if (row, col) in {(0, 0), (0, last), (last, 0), (last, last)}:
        score += 1000
    if row in (0, last) or col in (0, last):
        score += 50
    return score


def corner_count(board: Board, player: str) -> int:
    last = board.size - 1
    corners = ((0, 0), (0, last), (last, 0), (last, last))
    return sum(board.get(row, col) == player for row, col in corners)


def edge_stability_like_count(board: Board, player: str) -> int:
    last = board.size - 1
    corners_and_directions = (
        ((0, 0), (0, 1), (1, 0)),
        ((0, last), (0, -1), (1, 0)),
        ((last, 0), (0, 1), (-1, 0)),
        ((last, last), (0, -1), (-1, 0)),
    )

    stable = set()
    for corner, horizontal, vertical in corners_and_directions:
        if board.get(*corner) != player:
            continue
        stable.add(corner)
        stable.update(edge_run(board, player, corner, horizontal))
        stable.update(edge_run(board, player, corner, vertical))
    return len(stable)


def edge_run(board: Board, player: str, start: Move, direction: Move) -> list[Move]:
    row, col = start
    dr, dc = direction
    result: list[Move] = []
    row += dr
    col += dc
    while board.inside(row, col) and board.get(row, col) == player:
        result.append((row, col))
        row += dr
        col += dc
    return result


def corner_adjacent_empty_count(board: Board, player: str) -> int:
    last = board.size - 1
    groups = (
        ((0, 0), ((0, 1), (1, 0), (1, 1))),
        ((0, last), ((0, last - 1), (1, last), (1, last - 1))),
        ((last, 0), ((last - 1, 0), (last, 1), (last - 1, 1))),
        ((last, last), ((last - 1, last), (last, last - 1), (last - 1, last - 1))),
    )
    count = 0
    for corner, adjacent in groups:
        if board.get(*corner) != ".":
            continue
        count += sum(board.get(row, col) == player for row, col in adjacent)
    return count
