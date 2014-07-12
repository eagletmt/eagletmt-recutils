require 'elasticsearch'
require 'time'
require 'sinatra/base'
require 'slim'
require 'tilt'

class SearchResult < Struct.new(:program, :captions, :score)
end

class App < Sinatra::Base
  get '/' do
    slim :index
  end

  PER_PAGE = 20

  get '/search' do
    @query = params[:q]
    page = params[:page] || 0
    @page = [page.to_i, 0].max
    @per_page = PER_PAGE
    @search_results = search(@query, @page)
    slim :search
  end

  helpers do
    def search(query, page)
      ret = elasticsearch.search(index: 'captions', body: {
        query: {
          query_string: {
            query: query,
            default_field: 'captions',
            default_operator: 'and',
          },
        },
        size: PER_PAGE,
        from: page * PER_PAGE,
        highlight: {
          fields: {
            captions: {
              pre_tags: ['<b>'],
              post_tags: ['</b>'],
            },
          },
        },
      })
      @search_total = ret['hits']['total']
      ret['hits']['hits'].map do |hit|
        SearchResult.new.tap do |r|
          source = hit['_source']
          r.program = format_filename(source['filename'])
          r.score = hit['_score']
          r.captions = hit['highlight']['captions']
        end
      end
    end

    def elasticsearch
      Elasticsearch::Client.new
    end

    def format_filename(filename)
      filename.gsub(/\A\d+_\d+ /, '').gsub(/ at .+\z/, '')
    end
  end
end
