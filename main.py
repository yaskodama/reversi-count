from __future__ import annotations

import argparse
import sys

from othello import board_after_moves, format_move, parse_position
from search import find_best_move
from smv_export import write_smv


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Find the best Othello/Reversi move.")
    parser.add_argument("position_file", help="text file containing board and turn line")
    parser.add_argument("--depth", type=int, default=None, help="search depth")
    parser.add_argument("--smv-out", default=None, help="write a NuSMV/nuXmv .smv file")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        with open(args.position_file, encoding="utf-8") as file:
            board, turn = parse_position(file.read())
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print("Board:")
    print(board.to_text())
    print(f"Turn: {turn}")
    print()

    legal = board.legal_moves(turn)
    if not legal:
        print("Legal moves: none (pass)")
        if args.smv_out:
            write_smv(args.smv_out, board, turn)
            print(f"SMV written: {args.smv_out}")
        return 0

    print("Legal moves:")
    for move, next_board in board_after_moves(board, turn):
        print(f"- {format_move(move)}")
        print(next_board.to_text())
        print()

    result = find_best_move(board, turn, args.depth)
    if result.move is None:
        print("Best move: pass")
    else:
        print(f"Best move: {format_move(result.move)}")
    print(f"Score: {result.score}")
    print(f"Depth: {result.depth}")
    print(f"Nodes: {result.nodes}")

    if args.smv_out:
        write_smv(args.smv_out, board, turn)
        print(f"SMV written: {args.smv_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
