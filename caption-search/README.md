# caption-search

ass ファイルから字幕をインポートして Elasticsearch で検索できるようにする。
なおより便利に使うには ass のファイル名にタイトル等のメタ情報がのっていることを前提にしている。

## Requirements

- elasticsearch
- elasticsearch-analysis-kuromoji
    - `elasticsearch-plugin --install elasticsearch/elasticsearch-analysis-kuromoji/2.2.0`

## Usage
最初に init.rb を実行して mapping を作っておく。

```
bundle exec ./init.rb
```

ass ファイルをインポートするときは import.rb を使う。

```
bundle exec ./import.rb /path/to/ass/*.ass
```

検索するときは rackup で立ち上がる Web UI を使う。

```
bundle exec rackup &
open http://localhost:9292/
```

## Search
Lucene の文法を受け付けるようになってるので、`穏やか -filename:アイカツ` のように検索できる。
