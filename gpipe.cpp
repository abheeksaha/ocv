#include <stdio.h>
#include <glib.h>

#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>

#include <gst/gst.h>
#include <gst/gstdebugutils.h>
#include <gst/gstbin.h>
#include <plugins/elements/gsttee.h>
#include "gpipe.h"


char outdesc[] = "\
rtpbin name=rtpgst0 \
rtpgst0.send_rtp_src_0 ! queue ! r3psink name=r3p0 port=50013 \
rtpgst0.send_rtp_src_1 ! queue ! r3psink name=r3p1 port=50018 \
udpsrc name=rtcpsrc0 port=50311 ! rtpgst0.recv_rtcp_sink_0 \
udpsrc name=rtcpsrc1 port=50312 ! rtpgst0.recv_rtcp_sink_1 \
tpoint.src_1 ! parsebin ! rtph264pay name=vppy ! queue name=vsq ! rtpgst0.send_rtp_sink_1 \
dcvMod.rtp_src ! queue name=dsq ! application/x-rtp,media=application,payload=102 ! rtpgstpay name=rgpy pt=102 ! rtpgst0.send_rtp_sink_0" ;

char indesc[] = "\
rtpsession name=rtpgst0 \
rtpsession name=rtpvid1 \
r3psrc name=r3r0 port=50013 ! rtpgst0.recv_rtp_sink \
r3psrc name=r3r1 port=50018 ! rtpvid1.recv_rtp_sink \
rtpgst0.send_rtcp_src ! queue ! udpsink name=rtcpsrc0 port=50310 \
rtpvid1.send_rtcp_src ! queue ! udpsink name=rtcpsrc1 port=50311 \
rtpvid1.recv_rtp_src ! application/x-rtp,media=video,clock-rate=90000 ! rtph264depay name=vsd ! parsebin ! tee name=tpoint \
rtpgst0.recv_rtp_src ! application/x-rtp,media=application,clock-rate=90000 ! rtpgstdepay name=rgpd ! application/x-rtp ! dcvMod.rtp_sink " ;

char procdesc[] = "\
tpoint.src_0 ! queue name=ddq ! avdec_h264 ! videoconvert ! video/x-raw,format=BGR ! videoscale ! dcvMod.video_sink \
dcvMod.video_src ! video/x-raw,format=BGR ! queue !" ;


gboolean configurePortsOutdesc(int txport, char * destIp, GstElement *bin)
{
	GstElement *r3psink, *udps;
	g_print("Setting destination to %s:%u\n",destIp,txport) ;
	r3psink  = gst_bin_get_by_name(GST_BIN(bin),"r3p0") ;
	if (r3psink != NULL ) {
		g_object_set(G_OBJECT(r3psink),"host",destIp, NULL) ;
		g_object_set(G_OBJECT(r3psink),"port",txport, NULL) ;
	}
	else return FALSE ;
	r3psink  = gst_bin_get_by_name(GST_BIN(bin),"r3p1") ;
	if (r3psink != NULL ) {
		g_object_set(G_OBJECT(r3psink),"host",destIp, NULL) ;
		g_object_set(G_OBJECT(r3psink),"port",txport+2, NULL) ;
	}
	else return FALSE ;
	udps = gst_bin_get_by_name(GST_BIN(bin),"rtcpsrc0") ;
	if (udps == NULL) return FALSE ;
	g_object_set(G_OBJECT(udps),"port",txport+1, NULL) ;
	udps = gst_bin_get_by_name(GST_BIN(bin),"rtcpsrc1") ;
	if (udps == NULL) return FALSE ;
	g_object_set(G_OBJECT(udps),"port",txport+3, NULL) ;
	g_print("Setting gst tx port %u, video tx port %u, gst rtcp rx port %u, video rtcp rx port %u\n",
		txport,txport+2,txport+1,txport+3) ;
	return TRUE;
}

gboolean configurePortsIndesc(int txport, char * srcIp, GstElement *bin)
{
	GstElement *r3psink, *udps;
	g_print("Setting destination to %s:%u\n",srcIp,txport) ;
	r3psink  = gst_bin_get_by_name(GST_BIN(bin),"r3r0") ;
	if (r3psink != NULL ) {
		g_object_set(G_OBJECT(r3psink),"host",srcIp, NULL) ;
		g_object_set(G_OBJECT(r3psink),"port",txport, NULL) ;
	}
	else return FALSE ;
	r3psink  = gst_bin_get_by_name(GST_BIN(bin),"r3r1") ;
	if (r3psink != NULL ) {
		g_object_set(G_OBJECT(r3psink),"host",srcIp, NULL) ;
		g_object_set(G_OBJECT(r3psink),"port",txport+2, NULL) ;
	}
	else return FALSE ;
	udps = gst_bin_get_by_name(GST_BIN(bin),"rtcpsrc0") ;
	if (udps == NULL) return FALSE ;
	g_object_set(G_OBJECT(udps),"port",txport+1, NULL) ;
	udps = gst_bin_get_by_name(GST_BIN(bin),"rtcpsrc1") ;
	if (udps == NULL) return FALSE ;
	g_object_set(G_OBJECT(udps),"port",txport+3, NULL) ;
	g_print("Setting gst tx port %u, video tx port %u, gst rtcp rx port %u, video rtcp rx port %u\n",
		txport,txport+2,txport+1,txport+3) ;
	return TRUE;

}
