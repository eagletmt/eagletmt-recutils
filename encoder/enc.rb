#!/usr/bin/env ruby
require 'json'
require 'open3'
require 'pathname'
require 'tempfile'

def get_duration(path)
  outbuf, errbuf, status = Open3.capture3('ffprobe', '-show_format', '-print_format', 'json', path.to_s)
  if status.success?
    format = JSON.parse(outbuf)['format']
    format['duration'].to_f
  else
    raise "Error(#{status.exitstatus}): #{errbuf}"
  end
end

EPS = 1.0

def verify_audio_and_video!(mp4_path)
  Tempfile.open(['audio', '.mp4']) do |audio_file|
    audio_file.close
    unless system('ffmpeg', '-y', '-i', mp4_path.to_s, '-vn', '-acodec', 'copy', audio_file.path)
      raise 'ffmpeg -vn failed'
    end

    Tempfile.open(['video', '.mp4']) do |video_file|
      video_file.close
      unless system('ffmpeg', '-y', '-i', mp4_path.to_s, '-an', '-vcodec', 'copy', video_file.path)
        raise 'ffmpeg -an failed'
      end

      audio_duration = get_duration(audio_file.path)
      video_duration = get_duration(video_file.path)
      if (audio_duration - video_duration).abs > EPS
        raise "Duration mismatch! audio:#{audio_duration} video:#{video_duration}"
      end
    end
  end
  true
end

AUDIO_OPTS = %w[-strict experimental -acodec aac -ac 2 -ar 48000 -ab 128k]
VIDEO_OPTS = %w[-vcodec libx264 -aspect 16:9 -filter:v yadif -s 1280x720 -crf 21 -b_strategy 2 -me_method umh -refs 8 -subq 7 -trellis 2 -deblock 1:1]
MUX_OPTS = %w[-f mp4]

def encode(src_path, dst_path)
  start = Time.now
  puts "Start #{start}"
  ret = system('ffmpeg', '-i', src_path.to_s, *AUDIO_OPTS, *VIDEO_OPTS, *MUX_OPTS, dst_path.to_s)
  finish = Time.now
  puts "Finish #{finish}"
  elapsed = finish - start
  sec = elapsed % 60
  elapsed /= 60
  min = elapsed % 60
  hour = elapsed / 60
  printf("  Elapsed %dh%02dm%02ds\n", hour, min, sec)
  ret
end

ts_path = Pathname.new(ARGV[0])
mp4_path = ts_path.sub_ext('.mp4')
ts_duration = get_duration(ts_path)
unless encode(ts_path, mp4_path)
  abort "Encode failure!"
end
mp4_duration = get_duration(mp4_path)
if (ts_duration - mp4_duration).abs > EPS
  abort "Duration mismatch: TS #{ts_duration}, MP4 #{mp4_duration}"
end

verify_audio_and_video!(mp4_path)

orig_fname = ts_path.basename.to_s[/\A\d+_\d+/, 0]
orig_path = ts_path.parent.join("#{orig_fname}.ts")

ts_path.unlink
orig_path.unlink
