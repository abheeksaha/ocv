#!/bin/sh
gst-launch-1.0 filesrc location=v1.webm ! matroskademux ! vp9dec ! videoconvert ! videoscale ! videorate ! video/x-raw,framerate=10/1 ! vp9enc ! rtpvp9pay ! udpsink host=192.168.1.71 port=50017
