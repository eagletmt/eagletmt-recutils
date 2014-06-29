# assdumper
TS の字幕情報を抽出して [.ass](http://en.wikipedia.org/wiki/SubStation_Alpha) の形式で出力する。

assdumper は実時間で字幕のタイミングを出力します。
実際に使うときは assadjust.rb に録画開始時刻を与えて相対時間に直す必要があります。

```
% assdumper precure.ts > precure.raw.ass
% assadjust.rb 8:29:45 precure.raw.ass > precure.ass
```
