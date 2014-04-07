#!/bin/sh
DB=1
QUEUE=jobs
TIMEOUT=0 # never
PT_DIR=/home/pt

while [ ! -r stop.txt ]
do
  FNAME="$(redis-cli -n $DB --raw BLPOP $QUEUE $TIMEOUT | tail -1)"
  echo "$FNAME"
  pushd "$PT_DIR"
  ./enc.sh "$FNAME".ts && rm "$FNAME".ts
  popd
done
