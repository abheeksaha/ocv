//gst-launch-1.0 -v udpsrc port=50019 
//	dmx.src_87 ! 


#include <stdio.h>
#include <ctype.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/rtpmanager/gstrtpptdemux.h>
#include <gst/udp/gstudpsrc.h>
#include <gst/app/app.h>

static void fsink_newsample(GstAppSink *slf, gpointer d) ;
static void fsink_newpreroll(GstAppSink *slf, gpointer d) ;
char *processdata(GstBuffer *b) ;
static void help()
{
}

extern gboolean listenToBus(GstElement *pipeline, GstState * playing, GstState *oldstate, unsigned int tms) ;
gboolean terminate ;
gboolean sigrcvd ;

typedef struct {
	GstElement *pipeline;
	GstElement *bin;
	GstPad *vp9extpad;
	GstPad *gstextpad;
	GstCaps *vcaps;
	GstCaps *dcaps;
} vpipe_t ;

static void demuxpadAdded(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void paddEventAdded(GstElement *s, GstPad *p, gpointer d) ;
static void paddEventRemoved(GstElement *s, GstPad *p, gpointer d) ;

static char mainq[] = " avdec_vp9 ! videoconvert ! videoscale !  autovideosink";
static char pns[1024] ;
static char fileq[] = " testsink\n" ;
static char fns[256] ;
#if 1
static char pipedesc[] = "udpsrc name=usrc address=192.168.1.71 port=50019 ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! rtpvp9depay name=vp9d ! queue ! avdec_vp9 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 ! autovideosink \
rpdmx.src_102 ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstdepay name=gstd ! queue ! appsink name=nxtrcv" ;
#else
static char pipedesc[] = "udpsrc name=usrc address=192.168.1.71 port=50019 ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! rtpvp9depay name=vp9d ! queue ! filesink location=op.webm \
rpdmx.src_102 ! application/x-rtp,medial=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstdepay name=gstd ! queue ! appsink name=nxtrcv" ;
#endif



int main( int argc, char** argv )
{

	vpipe_t D;
	GstStateChangeReturn ret;
	gboolean terminate = FALSE ;
	GstBus *bus;
	GstMessage *msg;
	help();

	gst_init(&argc, &argv) ;
#if 0
	if (argc > 1 && (strcmp(argv[1], "--test") == 0))
	{
		sprintf(pns,pipedesc ,fileq) ;
	}
	else {
		sprintf(pns, pipedesc , mainq) ;
	}
#endif
	g_print("Using main pipeline %s\n",pipedesc) ;
	D.pipeline = gst_pipeline_new("input") ;
	D.vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "VP9", NULL);
	D.dcaps = gst_caps_new_simple ( "application/x-rtp", "media", G_TYPE_STRING, "application", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
//	D.ecaps = gst_caps_new_simple ( "application/x-test", "capsversion", G_TYPE_INT, 3, NULL);
	{
		GError *error = NULL ;

		D.pipeline = gst_parse_launch(pipedesc,&error);
		if (error != NULL) {
			g_print("Error making pipeline:%s\n",error->message) ;
			g_error_free(error) ;
			exit(1) ;
		}
//    		gst_bin_add( GST_BIN(D.pipeline), D.bin) ;
		{
			GstElement * vp9depay = gst_bin_get_by_name( GST_BIN(D.pipeline),"vp9d") ; 
			D.vp9extpad = gst_element_get_static_pad(vp9depay ,"sink") ;
			GstCaps *cps;
			if (D.vp9extpad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(D.vp9extpad,NULL) ;
			g_print("Sink video can handle caps:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(D.vp9extpad,D.vcaps) ;
		}
		{
			GstElement *gstdepay = gst_bin_get_by_name( GST_BIN(D.pipeline),"gstd") ; 
			D.gstextpad  = gst_element_get_static_pad(gstdepay ,"sink") ;
			GstCaps *cps;

			cps = gst_pad_query_caps(D.gstextpad,NULL) ;
			g_print("Sink data can handle caps:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(D.gstextpad,D.dcaps) ;
		}
		{
			GstElement *fsink = gst_bin_get_by_name(GST_BIN(D.pipeline), "nxtrcv") ;
			g_signal_connect(GST_APP_SINK_CAST(fsink),"new-sample", fsink_newsample,&D) ;
			g_signal_connect(GST_APP_SINK_CAST(fsink),"new-preroll", fsink_newpreroll,&D) ;
			g_object_set(G_OBJECT(fsink), "emit-signals", TRUE,NULL) ;
		}

		GstCaps *caps = gst_caps_new_simple (
				"application/x-rtp", 
//				"media",G_TYPE_STRING, "video", 
				"clock-rate", G_TYPE_INT,90000, 
//				"encoding-name", G_TYPE_STRING, "VP9",
				NULL ) ;

		GstElement *usrc = gst_bin_get_by_name(GST_BIN(D.pipeline),"usrc") ;
		g_object_set(G_OBJECT(usrc),"port", 50019, NULL) ; 
		g_object_set(G_OBJECT(usrc),"address", "192.168.1.71", NULL) ; 
		g_object_set(G_OBJECT(usrc),"caps", caps, NULL) ; 

		GstElement *rtpdemux = gst_bin_get_by_name(GST_BIN(D.pipeline), "rpdmx") ;
		g_signal_connect(G_OBJECT(rtpdemux), "new-payload-type", G_CALLBACK(demuxpadAdded), &D) ;
//		g_signal_connect(rtpdemux, "removed-pt-pad", G_CALLBACK(demuxpadRemoved), &D) ;
		g_signal_connect(G_OBJECT(rtpdemux), "pad-added", G_CALLBACK(paddEventAdded), &D) ;
		g_signal_connect(G_OBJECT(rtpdemux), "pad-removed", G_CALLBACK(paddEventRemoved), &D) ;

	}
	ret = gst_element_set_state (D.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (D.pipeline);
		return -1;
	}
	while (!terminate)
	{
		GstState outputplaying,oldstate;
		terminate = listenToBus(D.pipeline,&outputplaying,&oldstate,5) ;
	}

    return 0;
}
static void paddEventAdded(GstElement *g, GstPad *p, gpointer d)
{
	g_print("%s pad added\n",GST_PAD_NAME(p)) ;
}

static void paddEventRemoved(GstElement *g, GstPad *p, gpointer d)
{
	g_print("%s pad removed\n",GST_PAD_NAME(p)) ;
}

static void demuxpadAdded(GstElement *s, guint pt, GstPad *P, gpointer d)
{
	vpipe_t *dp = (vpipe_t *)d;
	GstPad *vpd = dp->vp9extpad;
	GstPad *gpd = dp->gstextpad;
	GstPad *tpd = (pt == 102  ? gpd:vpd) ;
	GstCaps *tcps = (pt == 102 ? dp->dcaps:dp->vcaps) ;
	g_print("Received pad added signal for pt=%d:connected %s to pad %s\n",pt,GST_PAD_NAME(P),GST_PAD_NAME(tpd)) ;
	if (GST_PAD_IS_LINKED(P)) {
		g_print("Pad already linked!\n") ;
	}
	if (GST_PAD_IS_LINKED(tpd)) {
		g_print("SinkPad already linked!\n") ;
	}
	else gst_pad_link(P,tpd) ;
	gst_pad_set_caps(P,tcps) ;
	{
		GstCaps *t1 = gst_pad_query_caps(P,NULL) ;
		GstCaps *t2 = gst_pad_query_caps(tpd,NULL) ;
		g_print("Marrying caps:%s on sink to caps %s on source\n",
				gst_caps_to_string(t1), gst_caps_to_string(t2)) ;
	}
}

static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d)
{
	g_print("Received pad removed signal for pt=%d, %s\n",pt, GST_PAD_NAME(P)) ;
}

static void fsink_newpreroll(GstAppSink *slf, gpointer d)
{
	vpipe_t *D = (vpipe_t *)d ;
	GstSample *s = gst_app_sink_pull_preroll(slf) ;
	GstBuffer *b = gst_sample_get_buffer(s) ;
	GstCaps * caps= gst_sample_get_caps(s) ;
	g_print("Received preroll: %s\n",gst_caps_to_string(caps)) ;
	g_print("Data processed:%s\n", processdata(b)) ;
}

static void fsink_newsample(GstAppSink *slf, gpointer d)
{
	vpipe_t *D = (vpipe_t *)d ;
	GstSample *s = gst_app_sink_pull_sample(slf) ;
	GstBuffer *b = gst_sample_get_buffer(s) ;
	GstCaps * caps;
       	caps = gst_sample_get_caps(s) ;
	g_print("Received sample: %s\n",gst_caps_to_string(caps)) ;
	g_print("Data processed:%s\n", processdata(b)) ;
}

char *processdata(GstBuffer *b)
{
	char ret[1024] ;
	char *pret = ret;
	if (b == NULL) strcpy(ret , "") ;
	else {
		GstMemory * bmem = gst_buffer_get_all_memory(b) ;
		GstMapInfo vmap;
		unsigned long *dp;
		guint64 *offset;
		if (gst_memory_map(bmem, &vmap, GST_MAP_READ) != TRUE) { strcpy(ret , "Couldn't map memory in vbuffer") ; }
		if (vmap.size < sizeof(unsigned long) + sizeof(guint64)) 
			sprintf(ret, "Not enough data:%d",vmap.size) ;
		else{
			offset = (guint64 *)vmap.data ;
			dp = (unsigned long*)(vmap.data+sizeof(guint64)) ;
			sprintf(ret,"offset=%lu, data=%lu",*offset,*dp) ;
		}
		gst_memory_unmap(bmem,&vmap) ;
	}
	return pret ;
}
