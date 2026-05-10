# Othello Model Checker Helper

Pythonでオセロ（リバーシ）の合法手列挙、着手後盤面生成、minimax + alpha-beta pruning による最良手探索、NuSMV/nuXmv向け `.smv` 出力を行います。

## ファイル構成

- `othello.py`: 盤面表現、入力パース、合法手、石返し
- `search.py`: 評価関数、minimax + alpha-beta pruning
- `smv_export.py`: NuSMV/nuXmv 用 `.smv` 生成
- `main.py`: コマンドライン実行
- `examples/start8.txt`: 8x8 初期盤面
- `examples/start6.txt`: 6x6 初期盤面

## 入力形式

盤面は 8x8 または 6x6 です。

- `.`: 空き
- `X`: 黒
- `O`: 白
- 最後の行に `turn: X` または `turn: O`

例:

```text
........
........
........
...OX...
...XO...
........
........
........
turn: X
```

## 実行方法

標準ライブラリのみで動きます。

```bash
python main.py examples/start8.txt
```

環境によっては `python` の代わりに `python3` を使ってください。

```bash
python3 main.py examples/start8.txt
```

6x6 の例:

```bash
python main.py examples/start6.txt
```

探索深さを指定する場合:

```bash
python main.py examples/start8.txt --depth 6
```

`.smv` ファイルも出力する場合:

```bash
python main.py examples/start8.txt --smv-out position.smv
```

## 6x6 全探索カウント

6x6のゲーム数を効率よく数えるためのC++ bitboard実装を、シェルスクリプトからビルドして実行できます。

```bash
scripts/count6_full.sh
```

別の6x6入力ファイルを指定する場合:

```bash
scripts/count6_full.sh examples/start6.txt
```

短時間だけ速度を測る場合:

```bash
scripts/count6_full.sh examples/start6.txt --max-seconds 30
```

実行中は10秒ごとに、現在探索中の手数、到達した最深手数、呼び出し数、816億手を総量とした進捗率、1分/1時間/1日あたりの処理ペース、推定残り時間などを標準エラーに表示します。

## 評価関数

`search.py` の `evaluate()` は以下を使う簡易評価です。

- 石数差
- 角の占有
- 合法手数差
- 角から辺に沿って連続する石を安定石に近い要素として加点
- 空き角の隣にある石を軽く減点

終局では勝敗を大きく優先するスコアを返します。

## SMV 出力について

`smv_export.py` の `generate_smv()` / `write_smv()` は、現在局面から合法手を1手指した後の盤面を `next(...)` で表す NuSMV/nuXmv 用モデルを生成します。

現時点では NuSMV/nuXmv の実行までは行いません。
