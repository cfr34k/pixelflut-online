#!/bin/sh

./pixelflut 127.0.0.1 12345 | ffmpeg -y -f rawvideo -pix_fmt bgra -s 1024x768 -r 10 -i - /tmp/test.mp4
