#include <stdio.h>
#include <ctype.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>

static void help()
{
}

typedef enum {
	G_WAITING,
	G_BLOCKED
} srcstate_e ;

static void processbuffer(void *A, int isz, void *B, int osz) ;
typedef struct {
	GstElement *pipeline;
	GstElement *vdec;
	GstElement *mdmx;
	GstAppSink *vsink;
	GstAppSrc  *dsrc;
	/* Output elements **/
	GstElement *mux;
	GstElement *op;
	srcstate_e dsrcstate;
	gboolean eos;
	unsigned long vsinkq;
} dpipe_t ;


#include <getopt.h>

#include "gutils.hpp"
#include "rseq.hpp"
static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;
static void dataFrameWrite(GstAppSrc *s, guint length, gpointer data) ;
static void videoFrameWrite(GstAppSrc *s, guint length, gpointer data) ;
static void dataFrameStop(GstAppSrc *s,  gpointer data) ;
static void videoFrameStop(GstAppSrc *s,  gpointer data) ;
extern void walkPipeline(GstBin *bin) ;

#if VP9PAY
static char pipedesc[] = "filesrc location=v1.webm ! matroskademux name=mdmx ! tee name=tvs \
			  rtpmux name=mux ! udpsink name=usink \
			  tvs.src_0 ! queue ! rtpvp9pay name=vppy ! mux.sink_0 \
			  tvs.src_1 ! queue ! vp9dec name=vp9d ! videoconvert ! videoscale ! video/x-raw,height=480,width=640 ! appsink  name=vsink \
			  appsrc name=dsrc ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";
#else
#endif
int main( int argc, char** argv )
{

	dpipe_t D;
	GstStateChangeReturn ret;
	GError *gerr = NULL;
	char ch;
	extern char *optarg;
	guint txport = 50018;
	gboolean dumpPipe = FALSE ;
	GstCaps *vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "VP9", NULL);
	GstCaps *dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "payload", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	help();

	while ((ch = getopt(argc, argv, "p:d")) != -1) {
		if (ch == 'p')
		{
			txport = atoi(optarg) ; g_print("Setting txport\n") ; 
		}
		if (ch == 'd') 
		{
			dumpPipe = TRUE ;
		}
	}
	gst_init(&argc, &argv) ;
	g_print("Using txport = %u\n",txport) ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc ;

	gerr = NULL ;
	GError * error = NULL;
	D.pipeline = gst_parse_launch(pipedesc,&error);
	if (error != NULL) {
		g_print("Couldn't create pipeline:%s\n", error->message) ;
		g_error_free(error) ;
		exit(1) ;
	}
	if (D.pipeline == NULL) {
		g_printerr("Couldn't create sub-bins\n") ;
		exit(4) ;
	}
	D.vsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"vsink")) ;
	D.mdmx = gst_bin_get_by_name(GST_BIN(D.pipeline),"mdmx") ;
	D.vdec  = gst_bin_get_by_name(GST_BIN(D.pipeline),"tvs") ;
	D.eos = FALSE ;
	D.vsinkq=0 ;
	g_signal_connect(D.mdmx, "pad-added", G_CALLBACK(muxpadAdded), D.vdec) ;

	{
		GstElement *udpsink = gst_bin_get_by_name(GST_BIN(D.pipeline),"usink") ;
		g_object_set(G_OBJECT(udpsink),"host", "192.168.1.71", NULL) ; 
		g_object_set(G_OBJECT(udpsink),"port", txport , NULL) ; 
	}
	{
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"mux") ; g_assert(ge) ;
			GstCaps *t,*u;
	  		rtpsink1 = gst_element_get_request_pad(ge, "sink_%u") ;
	  		rtpsink2 = gst_element_get_request_pad(ge, "sink_%u") ;
			t = gst_pad_query_caps(rtpsink1,NULL) ;
			u = gst_pad_query_caps(rtpsink2,NULL) ;
			g_print("rtpsink1 likes caps: %s\n", gst_caps_to_string(t)) ;
//			gst_pad_set_caps(rtpsink1, gst_caps_new_simple ("application/x-rtp", NULL)) ;
			g_print("rtpsink2 likes caps: %s\n", gst_caps_to_string(u)) ;
//			gst_pad_set_caps(rtpsink2,gst_caps_new_simple ("application/x-rtp", NULL)) ;
		}
#if VP9PAY
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"vppy") ; g_assert(ge) ;
			g_object_set(G_OBJECT(ge), "pt", 96 ,NULL) ;
		}
#endif
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"rgpy") ; g_assert(ge) ;
			mqsrc = gst_element_get_static_pad(ge, "src") ;
			GstCaps *t = gst_pad_query_caps(mqsrc,NULL) ;
			g_print("Rtp GST Pay wants %s caps \n", gst_caps_to_string(t)) ;
			g_object_set(G_OBJECT(ge), "pt", 102 ,NULL) ;
		}
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"dsrc") ; g_assert(ge) ;
			D.dsrc = GST_APP_SRC_CAST(ge) ;
//		  	GstCaps *caps = gst_caps_new_simple ("application/x-raw", NULL);
//			gst_app_src_set_caps(D.dsrc,caps) ;
			g_object_set(G_OBJECT(D.dsrc), "format", GST_FORMAT_TIME,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "stream-type", GST_APP_STREAM_TYPE_STREAM,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "emit-signals", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "block", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "max-bytes", 300 ,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "do-timestamp", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "min-percent", 50 ,NULL) ;
			g_signal_connect(G_OBJECT(D.dsrc), "need-data", G_CALLBACK(dataFrameWrite), &D) ;
			g_signal_connect(G_OBJECT(D.dsrc), "enough-data", G_CALLBACK(dataFrameStop), &D) ;
		}
		{
			g_signal_connect(GST_APP_SINK_CAST(D.vsink),"new-sample", sink_newsample,&D.vsinkq) ;
			g_signal_connect(GST_APP_SINK_CAST(D.vsink),"new-preroll", sink_newpreroll,&D.vsinkq) ;
			g_signal_connect(GST_APP_SINK_CAST(D.vsink),"eos", eosRcvd,&D.eos) ;
			g_object_set(G_OBJECT(D.vsink), "emit-signals", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.vsink), "drop", FALSE, NULL) ;
			g_object_set(G_OBJECT(D.vsink), "wait-on-eos", TRUE, NULL) ;
		}
	}
	// Now link the pads
  	if (!rtpsink1 || !rtpsink2) { 
		  g_print("Couldn't get request pads\n") ; 
	}
#if VP9PAY
#endif
	else if (!mqsrc) {
		g_printerr("Couldn't get static pad for message \n") ;
	}
	else {
//		GstPadLinkReturn ret1 = gst_pad_link(vqsrc, rtpsink1) ;
//		if (GST_PAD_LINK_FAILED(ret1)) {
//			g_print("Couldn't link video to mux: ret=%d\n",ret1) ;
//			g_print("%s:%s vqsrc linked to %s:%s\n",
//					GST_ELEMENT_NAME(gst_pad_get_parent_element(vqsrc)),GST_PAD_NAME(vqsrc),
//					GST_ELEMENT_NAME(gst_pad_get_parent_element(gst_pad_get_peer(vqsrc))),GST_PAD_NAME(gst_pad_get_peer(vqsrc))) ;
//		}
		GstPadLinkReturn ret2 = gst_pad_link(mqsrc, rtpsink2) ;
		if (GST_PAD_LINK_FAILED(ret2)) {
			g_printerr("Couldn't link data to mux: ret=%d\n", ret2) ;
			g_print("%s:%s (mqsrc) linked to %s:%s\n",
					GST_ELEMENT_NAME(gst_pad_get_parent_element(mqsrc)),GST_PAD_NAME(mqsrc),
					GST_ELEMENT_NAME(gst_pad_get_parent_element(gst_pad_get_peer(mqsrc))),GST_PAD_NAME(gst_pad_get_peer(mqsrc))) ;
		}
	}
	/** Now we will walk the pipeline and dump the caps **/
	ret = gst_element_set_state (D.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (D.pipeline);
		return -1;
	}
	ret = gst_element_set_state(GST_ELEMENT_CAST(D.dsrc),GST_STATE_PLAYING) ;
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set data src to the playing state.\n");
		return -1;
	}
	D.dsrcstate = G_BLOCKED;
	GstClockTime dp,op;

	dp = gst_pipeline_get_latency(GST_PIPELINE_CAST(D.pipeline)) ;
	if (dp == GST_CLOCK_TIME_NONE) g_print("Couldn't get latency for incoming\n") ;
	else {
	g_print("Returned latency %.4g ms \n",
			(double)dp/1000000.0) ;
	}
	if (dumpPipe == TRUE) {
		g_print("Dumping pipeline\n") ;
		walkPipeline( GST_BIN(D.pipeline)) ;
		g_print("Dumping pipeline\n") ;
		walkPipeline( GST_BIN(D.pipeline)) ;
		return (4) ;
	}

	ret = gst_element_set_state(GST_ELEMENT_CAST(D.vsink),GST_STATE_PLAYING) ;
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set vsink to the playing state.\n");
		return -1;
	}

	gboolean terminate = FALSE ;
	GstState oldstate,newstate ;
	do {
		static gboolean capsprinter = FALSE;
		GstSample *dgs = NULL;
		GstBuffer *databuf ;
		GstBuffer *v = NULL ;
		GstMemory *vmem,*dmem;
		GstCaps * dbt = NULL ;
		GstStructure *dbinfo = NULL ;
		GstSegment *dbseg = NULL ;
		static gboolean sendBuffer = TRUE ;
		static guint vbufsnt = 0;

		terminate = listenToBus(D.pipeline,&newstate,&oldstate,5) ;
#if 0
		if (validstate(newstate) && validstate(oldstate) && newstate != oldstate )
			g_print("New:%s Old:%s\n",
					gst_element_state_get_name(newstate),
					gst_element_state_get_name(oldstate)) ;
#endif

		if (!terminate && !D.eos && newstate == GST_STATE_PLAYING) {
			if (D.dsrcstate == G_WAITING){
				GstSample * ind = gst_app_sink_try_pull_sample(D.vsink,GST_SECOND) ;
				if (ind == NULL) {
					g_print("No data\n") ;
					continue ;
				}
				v = gst_sample_get_buffer(ind) ;
				if (sendBuffer == TRUE && v!=NULL) {
					GstMapInfo vmap,dmap;
					unsigned long *pd ;
					if (capsprinter == FALSE && v != NULL) {
						dbt = gst_sample_get_caps(ind) ;
						dbinfo = gst_sample_get_info(ind);
						if (dbt == NULL) continue;
						gchar *gs = gst_caps_to_string(dbt);
						g_print("Data Received:%s\n",gs) ;
						g_free(gs) ;
						capsprinter=TRUE;
					}
					databuf = gst_buffer_new_allocate(NULL,(RSEQSIZE+2)*sizeof(unsigned long),NULL) ;
					vmem = gst_buffer_get_all_memory(v) ;
					dmem = gst_buffer_get_all_memory(databuf) ;
					if (gst_memory_map(vmem, &vmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in vbuffer\n") ; }
					if (gst_memory_map(dmem, &dmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in dbuffer\n") ; }
					g_print("Input:%d o/p:%d..",vmap.size,dmap.size) ;
					pd = (unsigned long *)dmap.data ;
					tagbuffer(vmap.data,vmap.size,pd,dmap.size) ;
					gst_memory_unmap(dmem,&dmap);
					gst_memory_unmap(vmem,&vmap);

// Add a message dat
					g_print("Pushing data buffer...") ;
					gst_app_src_push_buffer(D.dsrc,databuf) ;
					sendBuffer = TRUE ;
				}
UNREF:
				gst_sample_unref(ind) ;
			}
		}
	} while (terminate == FALSE) ;
	g_print("Exiting!\n") ;
}

#if 0
static void videoFrameWrite(GstAppSrc *s, guint length, gpointer data)
{
	dpipe_t *pD = (dpipe_t *)data ;
	g_print("%s Needs video\n", GST_ELEMENT_NAME(GST_ELEMENT_CAST(s))) ;
	pD->vsrcstate = G_WAITING;
}
#endif
static void dataFrameWrite(GstAppSrc *s, guint length, gpointer data)
{
	dpipe_t *pD = (dpipe_t *)data ;
	g_print("%s Needs data\n", GST_ELEMENT_NAME(GST_ELEMENT_CAST(s))) ;
	pD->dsrcstate = G_WAITING;
}

#if 0
static void videoFrameStop(GstAppSrc *s, gpointer data)
{
	dpipe_t *pD = (dpipe_t *)data ;
	guint64 vbklog = gst_app_src_get_current_level_bytes(pD->vsrc) ;
	g_print("%s full (%lu)\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(s)),vbklog) ;
	pD->vsrcstate = G_BLOCKED;
}
#endif
static void dataFrameStop(GstAppSrc *s, gpointer data)
{
	dpipe_t *pD = (dpipe_t *)data ;
	g_print("%s full\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(s))) ;
	pD->dsrcstate = G_BLOCKED;
}

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D)
{
	GstPad *sinkpad;
	GstElement *dec = (GstElement *)D;
	g_print("Received pad added signal\n") ;
	sinkpad = gst_element_get_static_pad(dec ,"sink") ;
	GstPadLinkReturn ret = gst_pad_link(p, sinkpad) ;
	if (GST_PAD_LINK_FAILED(ret)) {
		g_printerr("Couldn't link data to mux: ret=%d\n", ret) ;
		g_print("%s:%s (p) linked to %s:%s\n",
				GST_ELEMENT_NAME(gst_pad_get_parent_element(p)),GST_PAD_NAME(p),
				GST_ELEMENT_NAME(gst_pad_get_parent_element(gst_pad_get_peer(p))),GST_PAD_NAME(gst_pad_get_peer(p))) ;
	}
	gst_pad_set_active(p,TRUE) ;
	gst_object_unref(sinkpad);
}

static void processbuffer(void *A, int isz, void *B, int osz)
{
	int icnt = isz/sizeof(unsigned long) ;
	unsigned long *pB = (unsigned long *)B;
	unsigned long *pA = (unsigned long *)A;
	int i;
	*pB = 0;
	for (i=0; i<icnt; i++)
		*pB = *pB^*pA++ ;
}