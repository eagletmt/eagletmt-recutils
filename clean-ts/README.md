# clean-ts
FFmpeg や MPlayer でうまく処理できるように MPEG-2 TS ファイルを変換する。

基本的には `ffmpeg -i infile.ts -acodec copy -vcodec copy -y outfile.ts` と同じように、主要なビデオストリームとオーディオストリームのみを残した MPEG-2 TS ファイルを生成する。
それに加えて、TOKYO MX におけるマルチ編成時の SD/HD 切り替えがファイルの先頭付近 (先頭から 200000 パケット) にあった場合、切り替え後から始まるように開始位置を調整する。

「主要な」ストリームを「id が最も小さい Program に属しているストリーム」としている。
FFmpeg に `av_find_best_stream` という API があり、これは解像度やビットレートから「主要な」ストリームを判断している。
ほとんどの場合 `av_find_best_stream` でうまくいくが、マルチ編成時にサブチャンネルを選択することがあったので、Program id を判断基準にしている。

## Install
```sh
cmake . -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=/usr
make install
```
