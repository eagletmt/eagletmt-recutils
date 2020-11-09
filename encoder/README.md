# encoder
[kaede](https://github.com/eagletmt/kaede) によって Redis に入れられた TS を

1. redis-to-sqs で SQS に入れ直す
2. sqs-encode でエンコード

という流れで処理する。適当なリカバリ用に encode がある。
