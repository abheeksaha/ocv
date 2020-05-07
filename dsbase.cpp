#include <stdio.h>
#include <ctype.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include <plugins/elements/gsttee.h>
#include "gutils.hpp"
#include "rseq.hpp"

static void help()
{
}

typedef enum {
	G_WAITING,
	G_BLOCKED
} srcstate_e ;

typedef enum {
	G_HASDATA,
	G_NODATA
} sinkstate_e ;

static void processbuffer(void *A, int isz, void *oB, int obsz, void *B, int bsz) ;
typedef struct {
	GstElement *pipeline;
	GstElement *vdec;
	GstAppSink *vsink;
	GstAppSink *dsink;
	/** Input elements **/
	GstCaps *vcaps,*dcaps;
	GstPad *vp9extpad, *teepad,*gstextpad;
	/* Output elements **/
	GstElement *mux;
	GstElement *op;
	GstAppSrc *dsrc;
	srcstate_e dsrcstate;
	unsigned int vsinkstate;
	unsigned int dsinkstate;
	gboolean eos;
	/** Data holding elements **/
	GQueue *dataqueue;
	GQueue *olddataqueue;
	GQueue *videoframequeue;
} dpipe_t ;
#define MAX_DATA_BUF_ALLOWED 240

static void eosRcvd(GstAppSink *slf, gpointer D) ;
static void demuxpadAdded(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void paddEventAdded(GstElement *s, GstPad *p, gpointer d) ;
static void paddEventRemoved(GstElement *s, GstPad *p, gpointer d) ;
#include <getopt.h>

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;
static void dataFrameWrite(GstAppSrc *s, guint length, gpointer data) ;
static void dataFrameStop(GstAppSrc *s,  gpointer data) ;
extern void walkPipeline(GstBin *bin) ;

static char srcdesc[] = "udpsrc name=usrc address=192.168.1.71 port=50018 ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! rtpvp9depay name=vp9d ! tee name=tpoint \
rtpmux name=mux ! queue !  udpsink name=usink host=192.168.1.71 port=50019 \
tpoint.src_0 !  queue ! rtpvp9pay ! mux.sink_0 \
tpoint.src_1 !  queue ! vp9dec name=vdec ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 ! tee name=2t \
	2t.src_0 ! queue ! appsink name=vsink \
	2t.src_1 ! queue ! testsink name=test \
rpdmx.src_102 !  rtpgstdepay name=rgpd ! appsink name=dsink \
appsrc name=dsrc !  application/x-rtp,medial=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";
#if 0
rpdmx.src_102 !  application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstdepay name=rgpd ! queue ! appsink name=dsink \
appsrc name=dsrc ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";
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
	gboolean stage1 = FALSE ;
	help();

	while ((ch = getopt(argc, argv, "p:ds:")) != -1) {
		if (ch == 'p')
		{
			txport = atoi(optarg) ; g_print("Setting txport\n") ; 
		}
		if (ch == 'd') 
		{
			dumpPipe = TRUE ;
		}
		if (ch == 's') {
			if (*optarg == '1') {
				stage1 = TRUE ;
			}
		}
	}
	gst_init(&argc, &argv) ;
	g_print("Using txport = %u\n",txport) ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc ;

	D.pipeline = gst_parse_launch(srcdesc, &gerr);	
	if (gerr != NULL) {
		g_print("Couldn't create pipeline:%s\n", gerr->message) ;
		g_error_free(gerr) ;
		exit(1) ;
	}
	D.dataqueue = g_queue_new() ;
	D.olddataqueue = g_queue_new() ;
	D.videoframequeue = g_queue_new() ;
	D.eos = FALSE ;

	D.vsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"vsink")) ;
	D.dsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"dsink")) ;

	D.vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "VP9", NULL);
	D.dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "payload", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	gerr = NULL ;
	{
		GstElement *udpsink = gst_bin_get_by_name(GST_BIN(D.pipeline),"usink") ;
		g_object_set(G_OBJECT(udpsink),"buffer-size", 1024*1024 , NULL) ; 
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
			g_print("rtpsink2 likes caps: %s\n", gst_caps_to_string(u)) ;
		}
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"rgpy") ; g_assert(ge) ;
			mqsrc = gst_element_get_static_pad(ge, "src") ;
			GstCaps *t = gst_pad_query_caps(mqsrc,NULL) ;
			g_print("Rtp GST Pay wants %s caps \n", gst_caps_to_string(t)) ;
			g_object_set(G_OBJECT(ge), "pt", 102 ,NULL) ;
		}
		{
#if 1
		  	GstCaps *caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "I420", "framerate", GST_TYPE_FRACTION, 20, 1,
						"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, "width", G_TYPE_INT, 848, "height", G_TYPE_INT, 480, NULL);
#endif
		}
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"dsrc") ; g_assert(ge) ;
			D.dsrc = GST_APP_SRC_CAST(ge) ;
//		  	GstCaps *caps = gst_caps_new_simple ("application/x-raw", NULL);
//			gst_app_src_set_caps(D.dsrc,caps) ;
			g_object_set(G_OBJECT(D.dsrc), "format", GST_FORMAT_TIME,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "stream-type", GST_APP_STREAM_TYPE_STREAM, NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "emit-signals", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "block", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "max-bytes", MAX_DATA_BUF_ALLOWED << 1 ,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "do-timestamp", FALSE ,NULL) ;
			g_object_set(G_OBJECT(D.dsrc), "min-percent", 50 ,NULL) ;
			g_signal_connect(G_OBJECT(D.dsrc), "need-data", G_CALLBACK(dataFrameWrite), &D) ;
			g_signal_connect(G_OBJECT(D.dsrc), "enough-data", G_CALLBACK(dataFrameStop), &D) ;
		}
		{
			g_signal_connect(GST_APP_SINK_CAST(D.vsink),"new-sample", sink_newsample,&D.vsinkstate) ;
			g_signal_connect(GST_APP_SINK_CAST(D.vsink),"new-preroll", sink_newpreroll,&D.vsinkstate) ;
			g_signal_connect(GST_APP_SINK_CAST(D.vsink),"eos", eosRcvd,&D.eos) ;
			g_object_set(G_OBJECT(D.vsink), "emit-signals", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.vsink), "drop", FALSE, NULL) ;
			g_object_set(G_OBJECT(D.vsink), "wait-on-eos", TRUE, NULL) ;
		}
		{
			g_signal_connect(GST_APP_SINK_CAST(D.dsink),"new-sample", sink_newsample,&D.dsinkstate) ;
			g_signal_connect(GST_APP_SINK_CAST(D.dsink),"new-preroll", sink_newpreroll,&D.dsinkstate) ;
			g_signal_connect(GST_APP_SINK_CAST(D.dsink),"eos", eosRcvd,&D) ;
			g_object_set(G_OBJECT(D.dsink), "emit-signals", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.dsink), "drop", FALSE, NULL) ;
			g_object_set(G_OBJECT(D.dsink), "wait-on-eos", TRUE, NULL) ;
		}
	/** Saving the depayloader pads **/
		{
			GstElement *teepoint = gst_bin_get_by_name( GST_BIN(D.pipeline),"tpoint") ; 
			GstCaps *cps;
			D.teepad = gst_element_get_static_pad(teepoint ,"sink") ;
			if (D.teepad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(D.teepad,NULL) ;
			g_print("Sink video can handle caps:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(D.teepad,D.vcaps) ;
		}
		{
			GstElement *vp9depay = gst_bin_get_by_name( GST_BIN(D.pipeline),"vp9d") ; 
			GstCaps *cps;
			D.vp9extpad = gst_element_get_static_pad(vp9depay ,"sink") ;
			if (D.vp9extpad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(D.vp9extpad,NULL) ;
			g_print("VP9 Depay can handle:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(D.vp9extpad,D.vcaps) ;
		}
		{
			GstElement *gstdepay = gst_bin_get_by_name( GST_BIN(D.pipeline),"rgpd") ; 
			D.gstextpad = gst_element_get_static_pad(gstdepay ,"sink") ;
			GstCaps *cps;
			if (D.gstextpad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(D.gstextpad,NULL) ;
			g_print("Sink data can handle caps:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(D.gstextpad,D.dcaps) ;
		}
		{
			GstElement *rtpdmx = gst_bin_get_by_name( GST_BIN(D.pipeline),"rpdmx") ;
			g_assert(rtpdmx) ;
			g_signal_connect(rtpdmx, "new-payload-type", G_CALLBACK(demuxpadAdded), &D) ;
			g_signal_connect(rtpdmx, "pad-added", G_CALLBACK(paddEventAdded), &D) ;
			g_signal_connect(rtpdmx, "pad-removed", G_CALLBACK(paddEventRemoved), &D) ;
		}
		{
			GstElement * usrc = gst_bin_get_by_name ( GST_BIN(D.pipeline), "usrc") ;
			g_assert(usrc) ;
			GstCaps *srccaps = gst_caps_new_simple (
				"application/x-rtp", 
				"clock-rate", G_TYPE_INT,90000, 
				NULL ) ;
//			g_object_set(G_OBJECT(usrc),"port", 50018, NULL) ; 
//			g_object_set(G_OBJECT(usrc),"address", "192.168.1.71", NULL) ; 
			g_object_set(G_OBJECT(usrc),"caps", srccaps, NULL) ; 
			g_object_set(G_OBJECT(usrc),"buffer-size", 100000, NULL) ; 
		}
		{
			GstElement * tp = gst_bin_get_by_name ( GST_BIN(D.pipeline), "tpoint") ;
			g_assert(tp) ;
			g_object_set(G_OBJECT(tp),"pull-mode",GST_TEE_PULL_MODE_SINGLE, NULL) ;
		}
	}

	// Now link the pads
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
	D.dsinkstate = 0;
	D.vsinkstate = 0;
	GstClockTime dp,op;

	dp = gst_pipeline_get_latency(GST_PIPELINE_CAST(D.pipeline)) ;
	op = gst_pipeline_get_latency(GST_PIPELINE_CAST(D.pipeline)) ;
	if (dp == GST_CLOCK_TIME_NONE) g_print("Couldn't get latency for incoming\n") ;
	if (op == GST_CLOCK_TIME_NONE) g_print("Couldn't get latency for incoming\n") ;
	else {
	g_print("Returned latency for incoming:%.4g ms outgoing:%.4g ms\n",
			(double)dp/1000000.0, 
			(double)op/1000000.0) ; 
	}
	if (dumpPipe == TRUE) {
		g_print("Dumping pipeline\n") ;
		walkPipeline( GST_BIN(D.pipeline)) ;
		return (4) ;
	}

	gst_element_set_state(GST_ELEMENT_CAST(D.dsrc),GST_STATE_PLAYING) ;
	gst_element_set_state(GST_ELEMENT_CAST(D.vsink),GST_STATE_PLAYING) ;
	gst_element_set_state(GST_ELEMENT_CAST(D.dsink),GST_STATE_PLAYING) ;

	gboolean terminate = FALSE ;
	GstState inputstate,oldstate ;
	do {
		static gboolean capsprinter = FALSE;
		GstSample *dgs = NULL;
		GstSample *vgs = NULL;
		GstCaps * dbt = NULL ;
		GstStructure *dbinfo = NULL ;
		GstSegment *dbseg = NULL ;
		static guint vbufsnt = 0;
		unsigned int numvideoframes=0;
		unsigned int numdataframes = 0;
		gboolean noproc = FALSE ;

		terminate = listenToBus(D.pipeline,&inputstate,&oldstate, 5) || (D.eos == TRUE) ;
		if (D.eos == TRUE) break ;
		if (inputstate >= GST_STATE_READY ) {
			gpointer videoFrameWaiting = NULL ;
			gpointer dataFrameWaiting = NULL ;
			GstClockTime vframenum, vframeref;
			guint64 *pd;

			if (D.vsinkstate > 0)
			{
				GstBuffer *v ;
				while ((vgs = gst_app_sink_try_pull_sample(D.vsink,GST_MSECOND)) != NULL)
				{
					v = gst_sample_get_buffer(vgs) ;
					g_queue_push_tail(D.videoframequeue,(gpointer) v) ;
					g_print("Pushing video frame : %lu (num elements=%d)\n", GST_BUFFER_PTS(v), g_queue_get_length(D.videoframequeue)) ;
					numvideoframes++;
					gst_sample_unref(vgs) ;
				}
				D.vsinkstate = 0 ;
			}
			if (D.dsinkstate > 0)
			{
				GstBuffer *d ;
				while ((dgs = gst_app_sink_try_pull_sample(D.dsink,GST_MSECOND)) != NULL)
				{
					d = gst_sample_get_buffer(dgs) ;
					g_queue_push_tail(D.olddataqueue,(gpointer) d) ;
					g_print("Pushing data frame : %lu (num elements=%d)\n", GST_BUFFER_PTS(d), g_queue_get_length(D.olddataqueue)) ;
					numdataframes++;
					gst_sample_unref(dgs) ;
				}
				D.dsinkstate = 0 ;
			}



			while (g_queue_get_length(D.videoframequeue) > 0 && g_queue_get_length(D.olddataqueue) > 0) 
			{
#if 1
				videoFrameWaiting = (GstBuffer *)g_queue_pop_head(D.videoframequeue) ;
				dataFrameWaiting = (GstBuffer *)g_queue_pop_head(D.olddataqueue) ;
				if (videoFrameWaiting == NULL || dataFrameWaiting == NULL) { g_print("Something wrong\n") ;break ; }
#endif
				{
					GstMemory *vmem,*dmem,*odmem;
					GstMapInfo vmap,odmap,dmap;
					gboolean match=FALSE ;
					vmem = gst_buffer_get_all_memory(videoFrameWaiting) ;
					odmem = gst_buffer_get_all_memory(dataFrameWaiting) ;
					if (gst_memory_map(vmem, &vmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in vbuffer\n") ; }
					if (gst_memory_map(odmem, &odmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in dbuffer\n") ; }
				/** Check for match **/

					if ( (match = matchbuffer (vmap.data,vmap.size,odmap.data,odmap.size)) != TRUE)
						g_printerr("Buffers didn't match\n",vframenum,vframeref);
					g_assert( match == TRUE) ;
					{
						GstBuffer *databuf = gst_buffer_new_allocate(NULL,sizeof(guint64) + odmap.size*sizeof(char) + sizeof(unsigned long),NULL) ;
						dmem = gst_buffer_get_all_memory(databuf) ;
						if (gst_memory_map(dmem, &dmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in dbuffer\n") ; }
						g_print("Input:%d,%d o/p:%d..\n",vmap.size,odmap.size, dmap.size) ;
						pd = (guint64 *)dmap.data;
						processbuffer(vmap.data,vmap.size,odmap.data,odmap.size,dmap.data,dmap.size) ;
						gst_memory_unmap(dmem,&dmap);

// Add a message dat
						g_print("Pushing data buffer...%d %d\n",D.vsinkstate,D.dsinkstate) ;
						GST_BUFFER_PTS(databuf) = GST_BUFFER_PTS(videoFrameWaiting) + GST_MSECOND*4  ;
						gst_app_src_push_buffer(D.dsrc,databuf) ;
					//	gst_buffer_unref(databuf) ;
					}
					gst_memory_unmap(vmem,&vmap) ;
					gst_memory_unmap(odmem,&odmap) ;
				}
			}
		}
	} while (terminate == FALSE) ;
	g_print("Exiting!\n") ;
}
/** Handlers **/

static void dataFrameWrite(GstAppSrc *s, guint length, gpointer data)
{
	dpipe_t *pD = (dpipe_t *)data ;
	gpointer bufd;
	gboolean srcfull = FALSE ;
	g_print("%s Needs data\n", GST_ELEMENT_NAME(GST_ELEMENT_CAST(s))) ;
	if (g_queue_get_length(pD->dataqueue) == 0) {
		pD->dsrcstate = G_WAITING;
	}
	while (((bufd = g_queue_pop_head(pD->dataqueue)) != NULL))
	{
		gst_app_src_push_buffer(s, GST_BUFFER_CAST(bufd)) ;
		if ( (srcfull = (gst_app_src_get_current_level_bytes(s) > MAX_DATA_BUF_ALLOWED)) == TRUE)

			break ;
	}
	if (srcfull == FALSE)
	pD->dsrcstate = G_WAITING;
	else 
	pD->dsrcstate = G_BLOCKED;

}

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
	gst_pad_link(p,sinkpad) ;
	gst_object_unref(sinkpad);
}

static void processbuffer(void *A, int isz, void *oB, int obsz, void *B, int bsz)
{
	int icnt;
	unsigned long *pB = (unsigned long *)B;
	unsigned long *pA = (unsigned long *)A;
	unsigned long *poB = (unsigned long *)oB;
	int i;
	icnt = obsz/sizeof(unsigned long) ;
	/** First copy the old buffer into the new buffer **/
	for (i=0; i<icnt; i++)
		*pB++ = *poB++ ;
	
	icnt = isz/sizeof(unsigned long) ;
	*pB = 0;
	for (i=0; i<icnt; i++)
	{
		*pB = *pB^*pA++ ;
	}
}

static gboolean pts_buffer_match(GstBuffer *v1, GstBuffer *v2, GstClockTime *p1, GstClockTime *p2)
{
	gboolean retval;
	*p1 = GST_BUFFER_PTS(v1) ;
	*p2 = GST_BUFFER_PTS(v2) ;
	if (!GST_BUFFER_PTS_IS_VALID(v1) || !GST_BUFFER_PTS_IS_VALID(v2)) {
		g_print("Pts Buffer Match: one of the buffers has no pts %s %s: ",
				GST_BUFFER_PTS_IS_VALID(v1) ? "valid":"invalid",
			       	GST_BUFFER_PTS_IS_VALID(v2) ? "valid":"invalid") ;

		retval = FALSE ;
	}
	else
	{
		g_print("PtsBuffer Match : pts1 = %.5g ms pts2 = %.5g ms: ",(double)(*p1)/1000000.0, (double)*p2/1000000.0) ;
		if (abs((long)(*p1 - *p2)) < 5*1000000) {
			if (*p1 > *p2) 
				GST_BUFFER_PTS(v2) = *p1 ;
			else if (*p2 < *p1) 
				GST_BUFFER_PTS(v1) = *p2 ;
			retval = TRUE ;
		}
		else
			retval = FALSE ;
	}
	g_print("returning %s\n", retval == TRUE? "true":"false") ;
	return retval;
}
				
/** demux pad handlers **/
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
	static char vpn[] = "sinkv" ;
	static char dpn[] = "sinkd" ;
	char *nm;
	dpipe_t *dp = (dpipe_t *)d;
	GstPad *vpd = dp->teepad;
	GstPad *gpd = dp->gstextpad;
	GstPad *tpd = (pt == 102  ? gpd:vpd) ;
	GstCaps *tcps = (pt == 102 ? dp->dcaps:dp->vcaps) ;
	nm = (pt == 102 ? dpn:vpn) ;
	g_print("Received pad added signal for pt=%d:connected %s to connect to pad %s\n",pt,GST_PAD_NAME(P),GST_PAD_NAME(tpd)) ;
	gst_pad_set_caps(P,tcps) ;
	gst_pad_set_active(P,TRUE) ;

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
