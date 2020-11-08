# clean-ts
FFmpeg や MPlayer でうまく処理できるように MPEG-2 TS ファイルを変換する。

基本的には `ffmpeg -i infile.ts -acodec copy -vcodec copy -y outfile.ts` と同じように、主要なビデオストリームとオーディオストリームのみを残した MPEG-2 TS ファイルを生成する。

「主要な」ストリームを「id が最も小さい Program に属しているストリーム」としている。
FFmpeg に `av_find_best_stream` という API があり、これは解像度やビットレートから「主要な」ストリームを判断している。
ほとんどの場合 `av_find_best_stream` でうまくいくが、マルチ編成時にサブチャンネルを選択することがあったので、Program id を判断基準にしている。

このプログラムは多重音声のケースにも対応している。ここでいう多重音声とは複数の音声ストリームが存在するものを指す。
番組本編が始まる前から音声ストリームが存在するものの、そのときのサンプルレートが0であり、直前になってから正常なサンプルレートのデータになり本編が多重音声で始まる、といったケースがある。
このような切り替えが発生する場合、FFmpeg はうまく扱うことができないので、適切な位置でカットする必要がある。

## Requirements
- ffmpeg >= 1.1

## Install
```sh
cmake . -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=/usr
make install
```

あるいは単純に

```sh
gcc -o clean-ts -O3 -lavcodec -lavformat -lavutil clean-ts.c
cp clean-ts /usr/bin/clean-ts
```
