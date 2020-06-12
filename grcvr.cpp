#include <stdio.h>
#include <ctype.h>

#include <unistd.h>
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
	g_print("Usage: %s -r <port number to listen on> [--mode [inter|first|last] -p txport -i txdest]\n",name) ;  
	g_print("If -p or -i is set, %s goes to relay mode\n",name) ;
	exit(1) ;
}

GstPadProbeReturn cb_have_data (GstPad  *pad, GstPadProbeInfo *info, gpointer user_data) ;
typedef struct {
	GstElement *pipeline;
	GstElement *vdec;
	GstAppSink *vsink;
	GstAppSink *dsink;
	GstAppSink *usink;
	GstAppSrc *usrc ;
	GstAppSrc *vdisp;
	GstAppSrc *dsrc;
	/** Input elements **/
	GstCaps *vcaps,*dcaps;
	/* Output elements **/
	srcstate_t usrcstate;
	srcstate_t dsrcstate;
	gboolean deos;
	gboolean deosSent;
	gboolean eosDsrc;
	gboolean veos;
	gboolean veosSent;
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
static char termdesc[] = "\
appsrc name=usrc ! application/x-rtp ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! queue ! rtpvp9depay name=vp9d ! avdec_vp9 name=vdec ! videoconvert ! video/x-raw,format=BGR ! videoscale ! queue ! appsink name=vsink \
rpdmx.src_102 ! queue ! rtpgstdepay name=rgpd ! appsink name=dsink \
appsrc name=vdisp ! queue ! video/x-raw,height=480,width=848,format=BGR ! autovideosink";

static char relaydesc[] = "\
appsrc name=usrc ! application/x-rtp ! queue ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! queue name=q1 ! rtpvp9depay name=vp9d ! tee name=tpoint \
rpdmx.src_102 ! queue name=q2 ! rtpgstdepay name=rgpd ! appsink name=dsink \
tpoint.src_0 ! queue name=q3 ! avdec_vp9 name=vdec ! videoconvert ! video/x-raw,format=BGR ! videoscale ! appsink name=vsink \
rtpmux name=mux ! queue name=q4 !  appsink name=usink \
appsrc name=vdisp ! queue name=q5 ! video/x-raw,height=480,width=848,format=BGR ! fakesink \
tpoint.src_1 ! queue name=q6 ! rtpvp9pay ! mux.sink_0 \
appsrc name=dsrc ! rtpgstpay name=rgpy ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! mux.sink_1";

typedef enum {
	GRCVR_FIRST,
	GRCVR_INTERMEDIATE,
	GRCVR_LAST
} grcvr_mode_e ;

extern bufferCounter_t inbc,outbc;
int dcvDebug=0;
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
	char *srcdesc = termdesc ;
	guint txport = 50020 ;
	char ipaddress[45] = "192.168.1.71" ;
	grcvr_mode_e grcvrMode = GRCVR_LAST ;

	guint ctr=0;
	int pktsout=0;
	struct timeval lastCheck;
	struct timezone tz;
	char clientipaddr[1024];
	static struct option longOpts[] = {
		{ "mode", required_argument, 0, 11 },
		{ "help", no_argument, 0, 'h' },
		{ "dcvDebug", optional_argument, 0, 12 },
		{ "recvport", required_argument, 0, 'r' },
		{ "sendport", required_argument, 0, 'p' },
		{ "sendaddr", required_argument, 0, 'i' },
		{ "localDisplay", required_argument, 0, 13 },
		{ 0,0,0,0 }} ;
	int longindex;

	Dv.num_frames = 0 ;
	while ((ch = getopt_long(argc, argv, "r:hp:i:",longOpts,&longindex)) != -1) {
		if (ch == 'r')
		{
			rxport = atoi(optarg) ; g_print("Setting rxport:%d\n",rxport) ; 
		}
		else if (ch == 'h') { help(argv[0]) ; exit(3) ; }
		else if (ch == 11) { 
			g_print("mode switched to:%s\n",optarg) ; 
			if (strncmp(optarg,"inter",5) == 0) { grcvrMode = GRCVR_INTERMEDIATE ; }
			else if (strncmp(optarg,"first",5) == 0) { grcvrMode = GRCVR_FIRST ; }
		}
		else if (ch == 12) { 
			g_print("dcvDebug on...") ; 
			if (optarg) { g_print("optarg=%s\n") ; dcvDebug = atoi(optarg) ;}
			else {dcvDebug = 1 ; g_print("\n") ; }
		}
		else if (ch == 'i') { strcpy(ipaddress,optarg) ; }
		else if (ch == 'p') { txport = atoi(optarg) ; }
		else ;
	}
	gst_init(&argc, &argv) ;
	GST_DEBUG_CATEGORY_INIT (my_category, "dcv", 0, "This is my very own");
	//bufferCounterInit(&inbc,&outbc) ;
	if (grcvrMode == GRCVR_INTERMEDIATE) srcdesc = relaydesc ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc ;

	D.pipeline = gst_parse_launch(srcdesc, &gerr);	
	if (gerr != NULL) {
		g_print("Couldn't create pipeline:%s\n", gerr->message) ;
		g_error_free(gerr) ;
		exit(1) ;
	}
	dcvBufQInit(&D.dataqueue) ;
	dcvBufQInit(&D.olddataqueue) ;
	dcvBufQInit(&D.videoframequeue) ;
	D.deos = FALSE ;
	D.veos = FALSE ;
	D.deosSent = FALSE ;
	D.veosSent = FALSE ;
	D.eosDsrc = FALSE;
	D.usrcstate.state = G_BLOCKED;
	D.usrcstate.length = 0;
	D.usrcstate.finished = FALSE ;
	D.dsrcstate.state = G_BLOCKED;
	D.dsrcstate.length = 0;
	D.dsrcstate.finished = FALSE ;


	/** Configure the end-points **/
	if (grcvrMode == GRCVR_LAST)
	{
		D.ftc = dcvFtConnInit("",rxport,NULL,-1) ;
	}
	else {
		D.ftc = dcvFtConnInit("", rxport, ipaddress,txport) ;
	}
	if (D.ftc == NULL) {
		g_print("Something went wrong in initialization\n") ;
		exit(3) ;
	}
	else if (dcvFtConnStart(D.ftc) == FALSE) {
		g_print("Something happened during connection start\n") ;
		exit(3) ;
	}
	else
		D.ftc->pclk = NULL ;

	gerr = NULL ;
	D.vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "VP9", NULL);
	D.dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "payload", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	D.vsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"vsink")) ;
	D.dsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"dsink")) ;
	dcvConfigAppSink(D.vsink,sink_newsample, &D.videoframequeue, sink_newpreroll, &D.videoframequeue,eosRcvd, &D.veos) ; 
	dcvConfigAppSink(D.dsink,sink_newsample, &D.olddataqueue, sink_newpreroll, &D.olddataqueue,eosRcvd, &D.deos) ; 
	if (grcvrMode == GRCVR_INTERMEDIATE) {
		D.usink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"usink")) ;
		dcvConfigAppSink(D.usink,dcvAppSinkNewSample, D.ftc, dcvAppSinkNewPreroll, D.ftc,dcvEosRcvd, &D.ftc) ; 
	}
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
		if (grcvrMode == GRCVR_INTERMEDIATE)
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"rgpy") ; g_assert(ge) ;
			mqsrc = gst_element_get_static_pad(ge, "src") ;
			GstCaps *t = gst_pad_query_caps(mqsrc,NULL) ;
			g_print("Rtp GST Pay wants %s caps \n", gst_caps_to_string(t)) ;
			g_object_set(G_OBJECT(ge), "pt", 102 ,NULL) ;
		}
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
		if (grcvrMode != GRCVR_LAST)
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"dsrc") ; g_assert(ge) ;
			D.dsrc = GST_APP_SRC_CAST(ge) ;
		  	GstCaps *caps = gst_caps_new_simple ("application/x-rtp",
		  		"media",G_TYPE_STRING,"application","clock-rate",G_TYPE_INT,90000,"payload",G_TYPE_INT,102,"encoding-name",G_TYPE_STRING,"X-GST",NULL) ;
			dcvConfigAppSrc(D.dsrc,dataFrameWrite,&D.dsrcstate,dataFrameStop,&D.dsrcstate, eosRcvdSrc, &D.eosDsrc,caps) ;
			g_object_set(G_OBJECT(D.dsrc), "is-live", TRUE,NULL) ;
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
	if ((grcvrMode != GRCVR_LAST))
	{	
		if ( (ret = gst_element_set_state(GST_ELEMENT_CAST(D.dsrc),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE )
			g_print("Couldn't set dsrc state to playing\n") ;
		else if ( (ret = gst_element_set_state(GST_ELEMENT_CAST(D.usink),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE )
			g_print("Couldn't set dsrc state to playing\n") ;
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
		gboolean activestate = (inputstate == GST_STATE_PAUSED || inputstate == GST_STATE_PLAYING || inputstate == GST_STATE_READY) ;

		terminate = listenToBus(D.pipeline,&inputstate,&oldstate, 25) ;
		ctr++ ;
		if (vbufsnt > 0 && !activestate) g_print("Inputstate gone inactive:%d\n",inputstate) ;
		if (activestate) {
			gpointer videoFrameWaiting = NULL ;
			gpointer dataFrameWaiting = NULL ;
			GstClockTime vframenum, vframeref;
			guint64 *pd;
			dcv_BufContainer_t *dataFrameContainer;
			struct timeval nowTime ;
			struct timezone nz;
			if (D.usrcstate.state == G_WAITING) {
				int bpushed ;
				dcvDebug = 3 ;
				if (D.usrcstate.finished != TRUE) {
					bpushed = dcvPushBytes(D.usrc,D.ftc,&D.usrcstate.finished) ;
					if (dcvDebug & 0x10)g_print("dcvPushBytes:Pushed %d bytes\n",bpushed ) ;
					if (D.usrcstate.finished == TRUE) {
						g_print("End of stream achieved\n") ;
					}
				}
				else { /** Connection closed from sender side, try and clear out the packets **/
					bpushed = dcvPushBuffered(D.usrc,D.ftc) ;
					if (dcvDebug & 0x10)g_print("dcvPushBytes:Pushed %d bytes\n",bpushed ) ;
					if (D.ftc->totalbytes == D.ftc->spaceleft) {
						if (D.ftc->eosSent == false) {
							g_print("End of TCP stream and no buffered bytes left\n") ;
//							gst_app_src_end_of_stream(D.usrc) ;
							D.ftc->eosSent = true ;
						}
					}
				}
				dcvDebug = 1 ;

			}
			while (D.ftc->totalbytes > D.ftc->spaceleft) {
				g_print("Pending data in input buffer! %d bytes\n",D.ftc->totalbytes - D.ftc->spaceleft) ;
				if (dcvPushBuffered(D.usrc,D.ftc)  <= 0) break ;
			}
			while (!g_queue_is_empty(D.videoframequeue.bufq) && (grcvrMode == GRCVR_FIRST || !g_queue_is_empty(D.olddataqueue.bufq))) 
			{
				int stay=0;
				ctr = 0 ;
				if (vfmatch == -1) {
					/** We failed last time, see if something has changed **/
					if ((dcvTimeDiff(D.videoframequeue.lastData,lastCheck) <= 0) &&
					    (dcvTimeDiff(D.olddataqueue.lastData,lastCheck) <= 0) )
						break ;
				}
				if (grcvrMode == GRCVR_FIRST) {
					dataFrameContainer = NULL ; 
				}
				else if ( (dataFrameContainer = (dcv_BufContainer_t *)g_queue_pop_head(D.olddataqueue.bufq)) == NULL) {
				       g_print("No data frame ...very strange\n") ;
				       break ;
				}

				else if ( ((vfmatch = dcvFindMatchingContainer(D.videoframequeue.bufq,dataFrameContainer)) == -1) ) {
					g_assert(vfmatch != -1) ;
					g_print("no match found: vfmatch=%d (vq=%u dq=%u)\n",vfmatch, g_queue_get_length(D.videoframequeue.bufq), g_queue_get_length(D.olddataqueue.bufq)) ;
#if 1
					if ( (stay = dcvLengthOfStay(dataFrameContainer)) > MAX_STAY)  {
						dcvBufContainerFree(dataFrameContainer) ;
						free(dataFrameContainer) ;
						g_print("Dropping data buffer, no match for too long\n") ;
					}
					else 
						g_queue_push_tail(D.olddataqueue.bufq,dataFrameContainer) ;
#endif
					gettimeofday(&lastCheck,&tz) ;
					g_print("Recording last failed check at %u:%u\n",lastCheck.tv_sec, lastCheck.tv_usec) ;
					continue ;
				}
GRCVR_PROCESS:
				{
					dcvStageFn_t stagef = (grcvrMode == GRCVR_LAST ? stage2:stagen) ;
					dcv_BufContainer_t *qe = ((dcv_BufContainer_t *)g_queue_pop_nth(D.videoframequeue.bufq,vfmatch)) ;
					GstBuffer * newVideoFrame = NULL ;
					GstBuffer * newDataFrame = NULL ;
					if (dataFrameContainer != NULL) 
						dataFrameWaiting = dataFrameContainer->nb;
					videoFrameWaiting = qe->nb ;
					GstCaps *vcaps = qe->caps ;
					newDataFrame = dcvProcessStage( videoFrameWaiting, vcaps,dataFrameWaiting, &Dv, stagef, &newVideoFrame ) ;
					if (localdisplay == TRUE) 
						if (dcvLocalDisplay(newVideoFrame,vcaps,D.vdisp,Dv.num_frames) != -1) Dv.num_frames++ ;
					g_print("State of video queue:%d\n",g_queue_get_length(D.videoframequeue.bufq)) ;
					dcvFtConnStatus(D.ftc) ;
					dcvAppSrcStatus(D.usrc,&D.usrcstate) ;
					

					if (grcvrMode == GRCVR_INTERMEDIATE) 
					{
						newDataFrame = gst_buffer_copy(dataFrameWaiting) ;
//						GstSample *gsample = gst_sample_new(newDataFrame, dataFrameContainer->caps,NULL,NULL) ;
						GstFlowReturn ret = gst_app_src_push_buffer(D.dsrc,newDataFrame) ;
						g_print("Pushing data frame .. retval=%d buffers=%d vq=%d dq=%d\n",
								ret,++vbufsnt,
								g_queue_get_length(D.videoframequeue.bufq),
								g_queue_get_length(D.olddataqueue.bufq)) ;
//						gst_sample_unref(gsample) ;
					}
					gst_buffer_unref(GST_BUFFER_CAST(videoFrameWaiting));
					gst_caps_unref(vcaps);
					gst_buffer_unref(GST_BUFFER_CAST(dataFrameWaiting));
				}
				/** Clean up the video frame queue **/
			}
			if (g_queue_is_empty(D.videoframequeue.bufq) && D.veos) {
				if (D.veosSent == false) {
					g_print("End of video stream\n") ;
					gst_app_src_end_of_stream(D.vdisp) ;
					D.veosSent = true ;
				}
			}
			if (g_queue_is_empty(D.olddataqueue.bufq) && D.deos) {
				if (D.deosSent == false) {
					g_print("End of data stream\n") ;
					gst_app_src_end_of_stream(D.dsrc) ;
					D.deosSent = true ;
				}
			}
			if (D.veosSent && D.deosSent )
			{
				g_print("Everything cleared up, exiting\n") ;
				terminate = TRUE ;
				dcvFtConnClose(D.ftc) ;
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
