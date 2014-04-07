#!/bin/bash

CORES=$(getconf _NPROCESSORS_ONLN)
INPUT="$1"
OUTPUT="$2"
if [ -z "$OUTPUT" ]; then
  OUTPUT=${INPUT%.ts}.mp4
fi

#AUDIO_OPTS='-acodec libfaac -ac 6 -ar 48000 -ab 128k'
AUDIO_OPTS='-strict experimental -acodec aac -ac 2 -ar 48000 -ab 128k'
VIDEO_OPTS='-vcodec libx264 -aspect 16:9 -filter:v yadif -s 1280x720 -crf 21 -b_strategy 2 -me_method umh -refs 8 -subq 7 -trellis 2 -deblock 1:1'
MUX_OPTS='-f mp4'
time ffmpeg -threads $CORES -i "$INPUT" $AUDIO_OPTS $VIDEO_OPTS $MUX_OPTS "$OUTPUT"
