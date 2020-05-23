#!/bin/sh
file="v1.webm"
destination="192.168.1.71"
port="50018"

if [ $# -gt 0 ]; then
	if [ $1 = "-h" ]; then
		echo "Usage: $0 [txport] [txipaddress] [filename]" ;
		exit ;
	else
		port=$1 ;
	fi
fi
if [ $# -gt 1 ]; then
	destination=$2 ;
fi
if [ $# -gt 2 ]; then
	file=$3;
fi 
echo "Using destination=$destination port=$port file=$file";

gst-launch-1.0 filesrc location=$file ! matroskademux ! vp9dec ! videoconvert ! videoscale ! videorate ! video/x-raw,framerate=10/1 ! vp9enc ! rtpvp9pay ! udpsink host=$destination port=$port
