#!/usr/bin/env ruby
require 'pathname'
require 'time'
require 'elasticsearch'

class Ass
  attr_reader :tid, :pid, :path, :dialogues

  def initialize(path)
    @path = path
    @dialogues = []
    @path.open do |f|
      parse(f)
    end
  end

  def parse(io)
    io.each_line do |line|
      m = line.match(/^Dialogue: 0,(\d{2}:\d{2}:\d{2}).\d{2},(\d{2}:\d{2}:\d{2}).\d{2},(.*)$/) rescue nil
      if m
        text = m[3].split(' ', 2).last
        if !text.empty?
          @dialogues << text
        end
      end
    end
  end

  def to_h
    {
      captions: dialogues,
      filename: path.basename.to_s,
    }
  end
end

ARGV.each do |arg|
  path = Pathname.new(arg)
  $stderr.puts "Importing #{path}"
  ass = Ass.new(path)
  Elasticsearch::Client.new.index(index: 'captions', type: 'captions', body: ass.to_h)
end
