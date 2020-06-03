#include <stdio.h>
#include <ctype.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include <plugins/elements/gsttee.h>
#include "gsftc.hpp"
#include "gutils.hpp"
#include "rseq.hpp"
#include "dsopencv.hpp"
#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"


#define MAX_STAY 1
static void help(char *name)
{
	g_print("Usage: %s -r <port number to listen on> \n",name) ;  
}

GstPadProbeReturn cb_have_data (GstPad  *pad, GstPadProbeInfo *info, gpointer user_data) ;
typedef struct {
	GstElement *pipeline;
	GstElement *vdec;
	GstAppSink *vsink;
	GstAppSink *dsink;
	GstAppSrc *usrc ;
	GstAppSrc *vdisp;
	/** Input elements **/
	GstCaps *vcaps,*dcaps;
	/* Output elements **/
	srcstate_t usrcstate;
	gboolean deos;
	gboolean veos;
	/** Data holding elements **/
	dcv_bufq_t dataqueue;
	dcv_bufq_t olddataqueue;
	dcv_bufq_t videoframequeue;
	dcv_ftc_t *ftc;
} dpipe_t ;
#define MAX_DATA_BUF_ALLOWED 240

static void eosRcvd(GstAppSink *slf, gpointer D) ;
static void demuxpadAdded(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void paddEventAdded(GstElement *s, GstPad *p, gpointer d) ;
static void paddEventRemoved(GstElement *s, GstPad *p, gpointer d) ;
#include <getopt.h>

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;

gboolean terminate =FALSE;
gboolean sigrcvd = FALSE ;
static char srcdesc[] = "\
appsrc name=usrc ! application/x-rtp ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! queue ! rtpvp9depay name=vp9d ! avdec_vp9 name=vdec ! videoconvert ! video/x-raw,format=BGR ! videoscale ! queue ! appsink name=vsink \
rpdmx.src_102 ! queue ! rtpgstdepay name=rgpd ! appsink name=dsink \
appsrc name=vdisp ! video/x-raw,height=480,width=848,format=BGR ! autovideosink";


extern bufferCounter_t inbc,outbc;
int main( int argc, char** argv )
{

	dpipe_t D;
	GstStateChangeReturn ret;
	GError *gerr = NULL;
	char ch;
	extern char *optarg;
	guint rxport = 50019;
	gboolean dumpPipe = FALSE ;
	gboolean stage1 = FALSE ;
	gboolean tx=TRUE ;
	gint vfmatch=0;
	static dcvFrameData_t Dv ;
	gboolean localdisplay = TRUE ;

	guint ctr=0;
	int pktsout=0;
	struct timeval lastCheck;
	struct timezone tz;
	char clientipaddr[1024];

	Dv.num_frames = 0 ;
	while ((ch = getopt(argc, argv, "r:h")) != -1) {
		if (ch == 'r')
		{
			rxport = atoi(optarg) ; g_print("Setting rxport:%d\n",rxport) ; 
		}
		if (ch == 'h') { help(argv[0]) ; exit(3) ; }
	}
	gst_init(&argc, &argv) ;
	GST_DEBUG_CATEGORY_INIT (my_category, "dcv", 0, "This is my very own");
	//bufferCounterInit(&inbc,&outbc) ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc ;

	D.pipeline = gst_parse_launch(srcdesc, &gerr);	
	if (gerr != NULL) {
		g_print("Couldn't create pipeline:%s\n", gerr->message) ;
		g_error_free(gerr) ;
		exit(1) ;
	}
	D.dataqueue.bufq = g_queue_new() ;
	D.olddataqueue.bufq = g_queue_new() ;
	D.videoframequeue.bufq = g_queue_new() ;
	D.deos = FALSE ;
	D.veos = FALSE ;
	D.usrcstate.state = G_BLOCKED;
	D.usrcstate.length = 0;
	D.usrcstate.finished = FALSE ;


	/** Configure the end-points **/
	{
		D.ftc = dcvFtConnInit("",rxport,NULL,-1) ;
		if (D.ftc == NULL) {
			g_print("Something went wrong in initialization\n") ;
			exit(3) ;
		}
		if (dcvFtConnStart(D.ftc) == FALSE) {
			g_print("Something happened during connection start\n") ;
			exit(3) ;
		}
		D.ftc->pclk = NULL ;
	}

	gerr = NULL ;
	D.vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "VP9", NULL);
	D.dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "payload", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	D.vsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"vsink")) ;
	D.dsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"dsink")) ;
	dcvConfigAppSink(D.vsink,sink_newsample, &D.videoframequeue, sink_newpreroll, &D.videoframequeue,eosRcvd, &D.veos) ; 
	dcvConfigAppSink(D.dsink,sink_newsample, &D.olddataqueue, sink_newpreroll, &D.olddataqueue,eosRcvd, &D.deos) ; 
	/** Saving the depayloader pads **/
	{
		{
			GstElement *vp9depay = gst_bin_get_by_name( GST_BIN(D.pipeline),"vp9d") ; 
			GstCaps *cps;
			GstPad * pad = gst_element_get_static_pad(vp9depay ,"sink") ;
			if (pad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(pad,NULL) ;
			g_print("VP9 Depay can handle:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(pad,D.vcaps) ;
		}
#if 0
		{
			pad = gst_element_get_static_pad (src, "src");
  			gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      						(GstPadProbeCallback) cb_have_data, NULL, NULL);
  			gst_object_unref (pad);
		}
#endif
		{
			GstElement *gstdepay = gst_bin_get_by_name( GST_BIN(D.pipeline),"rgpd") ; 
			GstPad * gstextpad = gst_element_get_static_pad(gstdepay ,"sink") ;
			GstCaps *cps;
			if (gstextpad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(gstextpad,NULL) ;
			g_print("Sink data can handle caps:%s\n", gst_caps_to_string(cps)) ;
			gst_pad_set_caps(gstextpad,D.dcaps) ;
		}
		{
			GstElement *rtpdmx = gst_bin_get_by_name( GST_BIN(D.pipeline),"rpdmx") ;
			g_assert(rtpdmx) ;
			g_signal_connect(rtpdmx, "new-payload-type", G_CALLBACK(demuxpadAdded), &D) ;
			g_signal_connect(rtpdmx, "pad-added", G_CALLBACK(paddEventAdded), &D) ;
			g_signal_connect(rtpdmx, "pad-removed", G_CALLBACK(paddEventRemoved), &D) ;
		}
		{
			D.usrc = GST_APP_SRC_CAST(gst_bin_get_by_name ( GST_BIN(D.pipeline), "usrc")) ;
			g_assert(D.usrc) ;
			GstCaps *srccaps = gst_caps_new_simple (
				"application/x-rtp", 
				NULL ) ;
			dcvConfigAppSrc(D.usrc,dataFrameWrite,&D.usrcstate, dataFrameStop,&D.usrcstate, eosRcvdSrc,&D.ftc->eosIn,srccaps) ;
			g_object_set(G_OBJECT(D.usrc), "is-live", TRUE,NULL) ;
			g_object_set(G_OBJECT(D.usrc), "do-timestamp", TRUE,NULL) ;
//			dcvAttachBufferCounterIn(GST_ELEMENT_CAST(D.usrc),&inbc) ;

		}
		{
			D.vdisp = GST_APP_SRC_CAST(gst_bin_get_by_name( GST_BIN(D.pipeline), "vdisp")) ;
			g_assert(D.vdisp) ;
			GstCaps *srccaps = gst_caps_new_simple (
				"video/x-raw", 
				NULL ) ;
		 	dcvConfigAppSrc(D.vdisp, NULL , NULL, NULL , NULL, NULL, NULL,srccaps) ;
		}
	}

	ret = gst_element_set_state(GST_ELEMENT_CAST(D.usrc),GST_STATE_PLAYING) ;
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("Unable to set data src to the playing state.\n");
		return -1;
	}

	ret = gst_element_set_state (D.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (D.pipeline);
		return -1;
	}

	if ( ( ret = gst_element_set_state(GST_ELEMENT_CAST(D.vsink),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE)
	{
		g_print("Couldn't set vsink state to playing\n") ;
	}
	if ( ( ret = gst_element_set_state(GST_ELEMENT_CAST(D.dsink),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE) 
	{
		g_print("Couldn't set dsink state to playing\n") ;
	}

	terminate = FALSE ;
	GstState inputstate,oldstate ;
	do {
		GstSample *dgs = NULL;
		GstSample *vgs = NULL;
		GstCaps * dbt = NULL ;
		GstStructure *dbinfo = NULL ;
		GstSegment *dbseg = NULL ;
		static guint vbufsnt = 0;
		unsigned int numvideoframes=0;
		unsigned int numdataframes = 0;
		gboolean noproc = FALSE ;

		terminate = listenToBus(D.pipeline,&inputstate,&oldstate, 25) || (D.veos == TRUE) || (D.deos == TRUE) ;
		if (D.veos == TRUE) break ;
		ctr++ ;
		if (inputstate == GST_STATE_PAUSED || inputstate == GST_STATE_PLAYING || inputstate == GST_STATE_READY) {
			gpointer videoFrameWaiting = NULL ;
			gpointer dataFrameWaiting = NULL ;
			GstClockTime vframenum, vframeref;
			guint64 *pd;
			dcv_BufContainer_t *dataFrameContainer;
			struct timeval nowTime ;
			struct timezone nz;
			if (D.usrcstate.state == G_WAITING) {
				int bpushed ;
				if (D.usrcstate.finished != TRUE) {
					bpushed = dcvPushBytes(D.usrc,D.ftc,&D.usrcstate.finished) ;
					if (bpushed)GST_INFO("dcvPushBytes:Pushed %d bytes\n",bpushed ) ;
					if (D.usrcstate.finished == TRUE) {
						g_print("End of stream achieved\n") ;
					}
				}
				else { /** Connection closed from sender side, try and clear out the packets **/
					bpushed = dcvPushBuffered(D.usrc,D.ftc) ;
					if (bpushed)GST_INFO("dcvPushBytes:Pushed %d bytes\n",bpushed ) ;
					if (D.ftc->totalbytes == D.ftc->spaceleft) {
						g_print("End of stream and no buffered bytes left\n") ;
						gst_app_src_end_of_stream(D.usrc) ;
					}
				}

			}
			while (!g_queue_is_empty(D.videoframequeue.bufq) && !g_queue_is_empty(D.olddataqueue.bufq)) 
			{
				int stay=0;
				ctr = 0 ;
				if (vfmatch == -1) {
					/** We failed last time, see if something has changed **/
					if ((dcvTimeDiff(D.videoframequeue.lastData,lastCheck) <= 0) &&
					    (dcvTimeDiff(D.olddataqueue.lastData,lastCheck) <= 0) )
						break ;
				}
				if ( (dataFrameContainer = (dcv_BufContainer_t *)g_queue_pop_head(D.olddataqueue.bufq)) == NULL) {
				       g_print("No data frame ...very strange\n") ;
				}

				if ( ((vfmatch = dcvFindMatchingContainer(D.videoframequeue.bufq,dataFrameContainer)) == -1) ) {
//						&& (vfmatch >= g_queue_get_length(D.videoframequeue.bufq))){
					GST_ERROR("no match found: vfmatch=%d (vq=%u dq=%u)\n",vfmatch, g_queue_get_length(D.videoframequeue.bufq), g_queue_get_length(D.olddataqueue.bufq)) ;
					if ( (stay = dcvLengthOfStay(dataFrameContainer)) > MAX_STAY)  {
						dcvBufContainerFree(dataFrameContainer) ;
						free(dataFrameContainer) ;
						GST_ERROR("Dropping data buffer, no match for too long\n") ;
					}
					else 
						g_queue_push_tail(D.olddataqueue.bufq,dataFrameContainer) ;

					gettimeofday(&lastCheck,&tz) ;
					GST_ERROR("Recording last failed check at %u:%u\n",lastCheck.tv_sec, lastCheck.tv_usec) ;
					continue ;
				}
				{
					dcv_BufContainer_t *qe = ((dcv_BufContainer_t *)g_queue_pop_nth(D.videoframequeue.bufq,vfmatch)) ;
					GstBuffer * newVideoFrame = NULL ;
					GstBuffer * newDataFrame = NULL ;
					dataFrameWaiting = dataFrameContainer->nb;
					videoFrameWaiting = qe->nb ;
					GstCaps *vcaps = qe->caps ;
					dcvProcessStage( videoFrameWaiting, vcaps,dataFrameWaiting, &Dv, stage2, &newVideoFrame ,&newDataFrame) ;
					if (localdisplay == TRUE) 
						if (dcvLocalDisplay(newVideoFrame,vcaps,D.vdisp,Dv.num_frames) != -1) Dv.num_frames++ ;
					/**
					if (newDataFrame != NULL) 
						dcvPushMetaData(newDataFrame) ;
						**/

						
					gst_buffer_unref(GST_BUFFER_CAST(videoFrameWaiting));
					gst_caps_unref(vcaps);
					gst_buffer_unref(GST_BUFFER_CAST(dataFrameWaiting));
				}
				/** Clean up the video frame queue **/
			}
		}
	} while (terminate == FALSE) ;
	g_print("Exiting!\n") ;
}
/** Handlers **/

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D)
{
	GstPad *sinkpad;
	GstElement *dec = (GstElement *)D;
	g_print("Received pad added signal\n") ;
	sinkpad = gst_element_get_static_pad(dec ,"sink") ;
	gst_pad_link(p,sinkpad) ;
	gst_object_unref(sinkpad);
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
#if 0
	GstPad *vpd = dp->vp9extpad;
	GstPad *gpd = dp->gstextpad;
	GstPad *tpd = (pt == 102  ? gpd:vpd) ;
#endif
	GstCaps *tcps = (pt == 102 ? dp->dcaps:dp->vcaps) ;
	nm = (pt == 102 ? dpn:vpn) ;
	g_print("Received pad added signal for pt=%d %s\n",pt,GST_PAD_NAME(P)) ;
	gst_pad_set_caps(P,tcps) ;
	gst_pad_set_active(P,TRUE) ;

#if 0
	if (GST_PAD_IS_LINKED(P)) {
		g_print("Pad already linked!\n") ;
	}
	if (GST_PAD_IS_LINKED(tpd)) {
		g_print("SinkPad already linked!\n") ;
	}
	else gst_pad_link(P,tpd) ;
#endif
	gst_pad_set_caps(P,tcps) ;
	{
		GstCaps *t1 = gst_pad_query_caps(P,NULL) ;
		GstCaps *t2 = gst_pad_query_caps(gst_pad_get_peer(P),NULL) ;
		g_print("Marrying caps:%s on %s to caps %s on %s\n",
				gst_caps_to_string(t1), 
				GST_PAD_NAME(P),
				gst_caps_to_string(t2),
				GST_PAD_NAME(gst_pad_get_peer(P))) ;
	}
}

static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d)
{
	g_print("Received pad removed signal for pt=%d, %s\n",pt, GST_PAD_NAME(P)) ;
}
