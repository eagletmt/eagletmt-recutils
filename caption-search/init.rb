#!/usr/bin/env ruby
require 'elasticsearch'

Elasticsearch::Client.new(hosts: 'localhost:9200').indices.create(index: 'captions', body: {
  mappings: {
    captions: {
      _timestamp: { enabled: false },
      _all: { enabled: false },
      properties: {
        captions: { type: 'string', store: true, index: 'analyzed', analyzer: 'kuromoji' },
        filename: { type: 'string', store: true, index: 'analyzed', analyzer: 'kuromoji' },
      },
    },
  },
})
