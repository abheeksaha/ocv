#!/bin/sh
file="cardrivingIndia.mkv"
destination="192.168.1.71"
port="50017"

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

gst-launch-1.0 rtpbin name=rbin \
	filesrc location=$file ! matroskademux ! parsebin ! rtph264pay ! application/x-rtp,media=video,clock-rate=90000 ! rbin.send_rtp_sink_0 \
	rbin.send_rtp_src_0 ! udpsink host=$destination port=$port
