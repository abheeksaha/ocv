#include <stdio.h>
#include <ctype.h>

#include <unistd.h>
#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include <plugins/elements/gsttee.h>
#include "gsftc.hpp"
#include "rseq.hpp"
#include "gutils.hpp"
#include "dsopencv.hpp"
#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"


#define MAX_STAY 1
static void help(char *name)
{
	g_print("Usage: %s -r <port number to listen on> [--mode [inter|first|last] -p txport -i txdest] -l|--localDisplay\n",name) ;  
	g_print("If -p or -i is set, %s goes to relay mode\n",name) ;
	exit(1) ;
}

#define DBG_FTC_MASK 0x0003
#define DBG_DEFAULT 0x0101
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
	gboolean eos[MAX_EOS_TYPES];
	gboolean eosSent[MAX_EOS_TYPES];
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
static dcvFrameData_t Dv ;
static char termdesc[] = "\
appsrc name=usrc ! application/x-rtp ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! queue ! rtph264depay name=vsd ! parsebin ! avdec_h264 name=vdec ! videoconvert ! video/x-raw,format=BGR ! videoscale ! queue ! appsink name=vsink \
rpdmx.src_102 ! queue ! rtpgstdepay name=rgpd ! appsink name=dsink \
appsrc name=vdisp ! queue ! video/x-raw,format=BGR ! %s";

static char relaydesc[] = "\
appsrc name=usrc ! application/x-rtp ! queue ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! queue name=q1 ! rtph264depay name=vsd ! queue ! tee name=tpoint \
rpdmx.src_102 ! queue name=q2 ! rtpgstdepay name=rgpd ! appsink name=dsink \
tpoint.src_0 ! queue name=q3 ! parsebin ! avdec_h264 name=vdec ! videoconvert ! video/x-raw,format=BGR ! videoscale ! appsink name=vsink \
rtpmux name=mux !  queue ! appsink name=usink \
appsrc name=vdisp ! queue name=q5 ! video/x-raw,format=BGR ! %s \
tpoint.src_1 ! queue ! parsebin ! rtph264pay ! mux.sink_0 \
appsrc name=dsrc ! rtpgstpay name=rgpy ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! mux.sink_1";

typedef enum {
	GRCVR_FIRST,
	GRCVR_INTERMEDIATE,
	GRCVR_LAST
} grcvr_mode_e ;

gboolean dcvQueuesLoaded(dpipe_t *pD, grcvr_mode_e grcvrMode) ;
int dcvProcessQueues(dpipe_t *pd, grcvr_mode_e grcvrMode, int vbufsnt, gboolean localdisplay, dcvFrameData_t *Dv) ;
int dcvPushToSink(dpipe_t *pD) ;
extern bufferCounter_t inbc,outbc;
extern int dcvFtcDebug;
extern int dcvGstDebug;
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
	gboolean localdisplay = false ;
	char *pdesc = termdesc ;
	char srcdesc[1024] ;
	guint txport = 50020 ;
	char ipaddress[45] = "192.168.1.71" ;
	grcvr_mode_e grcvrMode = GRCVR_LAST ;
	extern int strictCheck ;

	int pktsout=0;
	char clientipaddr[1024];
	static struct option longOpts[] = {
		{ "mode", required_argument, 0, 11 },
		{ "help", no_argument, 0, 'h' },
		{ "debug", optional_argument, 0, 'd' },
		{ "recvport", required_argument, 0, 'r' },
		{ "sendport", required_argument, 0, 'p' },
		{ "strictcheck", no_argument, 0, 18 },
		{ "sendaddr", required_argument, 0, 'i' },
		{ "localDisplay", required_argument, 0, 'l' },
		{ 0,0,0,0 }} ;
	int longindex;

	dcvFtcDebug = dcvGstDebug = 0 ;
	Dv.num_frames = 0;
	Dv.avgProcessTime = 0;

	while ((ch = getopt_long(argc, argv, "r:hp:i:l",longOpts,&longindex)) != -1) {
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
		else if (ch == 18) {
			strictCheck = 1 ;
		}
		else if (ch == 'd') { 
			int dbgFlags = 0 ;
			g_print("dbug on...") ; 
			if (optarg) { 
				g_print("optarg=%s\n",optarg) ; dbgFlags = atoi(optarg) ;
			}
			else { g_print("\n") ; dbgFlags = DBG_DEFAULT ;  }
			dcvFtcDebug = (dbgFlags & DBG_FTC_MASK) ;
			dcvGstDebug = ((dbgFlags >> 2) & DBG_FTC_MASK) ;
			g_print("dcvFtcDebug is set to %d, dcvGstDebug is set to %d\n",dcvFtcDebug,dcvGstDebug) ;
		}
		else if (ch == 'i') { strcpy(ipaddress,optarg) ; }
		else if (ch == 'p') { txport = atoi(optarg) ; }
		else if (ch == 'l') { localdisplay = true ; }
		else ;
	}
	gst_init(&argc, &argv) ;
	GST_DEBUG_CATEGORY_INIT (my_category, "dcv", 0, "This is my very own");
	if (grcvrMode == GRCVR_INTERMEDIATE) pdesc = relaydesc ;
	else if (grcvrMode == GRCVR_LAST) localdisplay = true ;
	sprintf(srcdesc,pdesc,(localdisplay == true? "autovideosink":"fakesink")) ;

	
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
	for (int i = 0; i<MAX_EOS_TYPES; i++)
	{
		D.eos[i] = FALSE ;
		D.eosSent[i] = FALSE ;
	}
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
	D.vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "H264", NULL);
	D.dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "payload", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	D.vsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"vsink")) ;
	D.dsink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"dsink")) ;
	dcvConfigAppSink(D.vsink,sink_newsample, &D.videoframequeue, sink_newpreroll, &D.videoframequeue,eosRcvd, &D.eos[EOS_VSINK]) ; 
	dcvConfigAppSink(D.dsink,sink_newsample, &D.olddataqueue, sink_newpreroll, &D.olddataqueue,eosRcvd, &D.eos[EOS_DSINK]) ; 
	if (grcvrMode == GRCVR_INTERMEDIATE) {
		D.usink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"usink")) ;
		dcvConfigAppSink(D.usink,dcvAppSinkNewSample, D.ftc, dcvAppSinkNewPreroll, D.ftc,dcvEosRcvd, &D.ftc) ; 
	}
	/** Saving the depayloader pads **/
	{
		{
			GstElement *vsdepay = gst_bin_get_by_name( GST_BIN(D.pipeline),"vsd") ; 
			GstCaps *cps;
			GstPad * pad = gst_element_get_static_pad(vsdepay ,"sink") ;
			if (pad == NULL) {
				g_printerr("No sink pad for vbin\n") ; 
			}
			cps = gst_pad_query_caps(pad,NULL) ;
			g_print("H264 Depay can handle:%s\n", gst_caps_to_string(cps)) ;
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
			dcvConfigAppSrc(D.usrc,dataFrameWrite,&D.usrcstate, dataFrameStop,&D.usrcstate, eosRcvdSrc,&D.eos[EOS_USRC],srccaps) ;
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
			dcvConfigAppSrc(D.dsrc,dataFrameWrite,&D.dsrcstate,dataFrameStop,&D.dsrcstate, eosRcvdSrc, &D.eos[EOS_DSRC],caps) ;
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
		if (vbufsnt > 0 && !activestate) g_print("Inputstate gone inactive:%d\n",inputstate) ;
		if (activestate) {
			GstClockTime vframenum, vframeref;
			guint64 *pd;
			int bpushed ;
			while (dcvQueuesLoaded(&D, grcvrMode))
			{
				int vfp = 0 ;
				vfp = dcvProcessQueues(&D,grcvrMode,vbufsnt,localdisplay,&Dv) ;
				if (vfp) g_print("Bytes received:%d\n", D.ftc->recvbytes) ;
				else 
					break ;
			}
	
			if (D.usrcstate.state == G_WAITING) {
				if (D.usrcstate.finished != TRUE) {
					static int lastbpushed = 0 ;
					bpushed = dcvPullBytesFromNet(D.usrc,D.ftc,&D.usrcstate.finished) ;
					if (bpushed != -1 || lastbpushed != -1) 
						if (dcvFtcDebug)
							g_print("dcvPullBytesFromNet:Pulled %d bytes vq=%d dq=%d\n",bpushed,
						      g_queue_get_length(D.videoframequeue.bufq),
						     g_queue_get_length(D.olddataqueue.bufq) ) ;
					lastbpushed = bpushed ;
#if 0
					if (bpushed < 0) 
					{
						usleep(1000) ;
						dcvPushToSink(&D) ;
					}
#endif
					if (D.usrcstate.finished == TRUE) {
						g_print("End of stream achieved, usrc state=%d\n",D.usrcstate.state) ;
						dcvFtcDebug=3 ;
					}
				}
				else if (dcvIsDataBuffered(D.ftc)){ /** Connection closed from sender side, try and clear out the packets **/
					static int lastbpushed = 0 ;
					bpushed = dcvPushBuffered(D.usrc,D.ftc) ;
					if (bpushed != -1 || lastbpushed != -1) 
						g_print("dcvPushbuffered:Pushed %d bytes vq=%d dq=%d\n",bpushed,
						      g_queue_get_length(D.videoframequeue.bufq),
						     g_queue_get_length(D.olddataqueue.bufq) ) ;
					lastbpushed = bpushed ;
					if (bpushed < -1) /** -1 indicates insufficient bytes in buffer. less than that means not enough space in outgoing **/
					{
						sleep(1) ;
						dcvPushToSink(&D) ;
					}
					if (D.ftc->totalbytes == D.ftc->spaceleft) {
						if (D.eosSent[EOS_USRC] == false) {
							g_print("End of TCP stream and no buffered bytes left\n") ;
							gst_app_src_end_of_stream(D.usrc) ;
							D.eosSent[EOS_USRC] = true ;
						}
					}
				}
			}
			while (dcvIsDataBuffered(D.ftc) && D.usrcstate.state == G_WAITING) {
				int bpushed ;
				if (dcvFtcDebug) g_print("%d bytes pending after end of cycle (state=%d)\n",dcvBufferedBytes(D.ftc),D.usrcstate.state) ; 
				if ((bpushed = dcvPushBuffered(D.usrc,D.ftc))  <= 0) {
					if (dcvFtcDebug) g_print("dcv Push Buffered has run out of space vq=%d dq=%d\n",
							g_queue_get_length(D.videoframequeue.bufq),g_queue_get_length(D.olddataqueue.bufq)) ; 
					if (bpushed == -2) sleep(1) ;
					break ;
				}
			}
			if (dcvIsDataBuffered(D.ftc)) { 
				if (dcvFtcDebug) g_print("%d bytes pending after very end of cycle (state=%d)\n",dcvBufferedBytes(D.ftc),D.usrcstate.state) ; 
				continue ;
			}
			
			if (grcvrMode == GRCVR_INTERMEDIATE && D.dsrcstate.state != G_WAITING) continue ;
			else if (D.usrcstate.state != G_WAITING) continue ;
			if (g_queue_is_empty(D.videoframequeue.bufq) && D.eos[EOS_VSINK]) {
				if (D.eosSent[EOS_VSINK] == false ) {
				g_print("End of video stream\n") ;
				D.eosSent[EOS_VSINK]=true ;
				}
			}
			if (g_queue_is_empty(D.olddataqueue.bufq) && D.eos[EOS_DSINK]) {
				if (D.eosSent[EOS_DSINK] == false ) {
				g_print("End of data stream\n") ;
				D.eosSent[EOS_DSINK]=true ;
				}
			}
			if ( (D.eosSent[EOS_DSINK] || D.eosSent[EOS_VSINK] ) &&
					g_queue_is_empty(D.videoframequeue.bufq) &&
					g_queue_is_empty(D.olddataqueue.bufq))
			{
					g_print("Everything cleared up, exiting\n") ;
					terminate = TRUE ;
					dcvFtConnClose(D.ftc) ;
			}
		}
	} while (terminate == FALSE) ;
	g_print("Exiting!\n") ;
}

int dcvPushToSink(dpipe_t *pD)
{
	GstFlowReturn vret,dret;
	int ret=0; 
	g_print ("Need to clear data from %s\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(pD->usrc))) ;
	if (pD->vsink && !gst_app_sink_is_eos(pD->vsink))
	{
		vret = sink_trypullsample(pD->vsink,&pD->videoframequeue) ;
		if (vret == GST_FLOW_ERROR) {
			g_print("Try pull sample failed for vsink\n") ;
		}
		else
			ret |= 1 ;
	}
	else
		g_print("vsink is eos!!\n") ;
	if (pD->dsink && !gst_app_sink_is_eos(pD->dsink))
	{
		dret = sink_trypullsample(pD->dsink,&pD->olddataqueue) ;
		if (dret == GST_FLOW_ERROR) {
			g_print("Try pull sample failed for dsink\n") ;
		}
		else
			ret |= 1 ;
	}
	else
		g_print("dsink is eos!!\n") ;
	return ret;
}

gboolean dcvQueuesLoaded(dpipe_t *pD, grcvr_mode_e grcvrMode)
{
	return (!g_queue_is_empty(pD->videoframequeue.bufq) && (grcvrMode == GRCVR_FIRST || !g_queue_is_empty(pD->olddataqueue.bufq)))  ;
}

int dcvProcessQueues(dpipe_t *pd, grcvr_mode_e grcvrMode, int vbufsnt, gboolean localdisplay, dcvFrameData_t *Dv)
{
	int stay=0;
	dcv_BufContainer_t *dataFrameContainer;
	struct timeval nowTime ;
	struct timezone nz;
	static struct timeval lastCheck ;
	static struct timezone tz;
	static gint vfmatch=0;
	gpointer videoFrameWaiting = NULL ;
	gpointer dataFrameWaiting = NULL ;
	tag_t T ;

	if (vfmatch == -1) {
		/** We failed last time, see if something has changed **/
		if ((dcvTimeDiff(pd->videoframequeue.lastData,lastCheck) <= 0) &&
		    (dcvTimeDiff(pd->olddataqueue.lastData,lastCheck) <= 0) )
						return 0 ;
	}
	else if (lastCheck.tv_sec == 0)
		gettimeofday(&lastCheck,&tz) ;

	if (grcvrMode == GRCVR_FIRST) {
			dataFrameContainer = NULL ; 
	}
	else if ( (dataFrameContainer = (dcv_BufContainer_t *)g_queue_pop_head(pd->olddataqueue.bufq)) == NULL) {
		       g_print("No data frame ...very strange\n") ;
		       return -1 ;
	}

	else if ( ((vfmatch = dcvFindMatchingContainer(pd->videoframequeue.bufq,dataFrameContainer,&T)) == -1) ) {
		g_print("no match found: vfmatch=%d (vq=%u dq=%u)\n",vfmatch, g_queue_get_length(pd->videoframequeue.bufq), g_queue_get_length(pd->olddataqueue.bufq)) ;
#if 1
		if ( (stay = dcvLengthOfStay(dataFrameContainer)) > MAX_STAY)  {
				dcvBufContainerFree(dataFrameContainer) ;
				free(dataFrameContainer) ;
				g_print("Dropping data buffer, no match for too long\n") ;
		}
		else 
			g_queue_push_head(pd->olddataqueue.bufq,dataFrameContainer) ;
#endif
		gettimeofday(&lastCheck,&tz) ;
		g_print("Recording last failed check at %u:%u\n",lastCheck.tv_sec, lastCheck.tv_usec) ;
		return 0 ;
	}
GRCVR_PROCESS:
	{
		dcvStageFn_t stagef = (grcvrMode == GRCVR_LAST ? stage2:stagen) ;
		dcv_BufContainer_t *qe = ((dcv_BufContainer_t *)g_queue_pop_nth(pd->videoframequeue.bufq,vfmatch)) ;
		GstBuffer * newVideoFrame = NULL ;
		GstBuffer * newDataFrame = NULL ;
		if (dataFrameContainer != NULL) {
			dataFrameWaiting = dataFrameContainer->nb;
			videoFrameWaiting = qe->nb ;
			GstCaps *vcaps = qe->caps ;
			newDataFrame = dcvProcessStage( videoFrameWaiting, vcaps,dataFrameWaiting, Dv, stagef, &newVideoFrame ) ;
			if (localdisplay == TRUE) 
			{
				if (dcvLocalDisplay(newVideoFrame,vcaps,pd->vdisp,Dv->num_frames) != -1) Dv->num_frames++ ;
			}
			else
				Dv->num_frames++ ;
			g_print("State of queues:vq=%d dq=%d\n", g_queue_get_length(pd->videoframequeue.bufq), 
					g_queue_get_length(pd->olddataqueue.bufq)) ;
					
				

			if (grcvrMode == GRCVR_INTERMEDIATE) 
			{
				newDataFrame = gst_buffer_copy(dataFrameWaiting) ;
				GstFlowReturn ret = gst_app_src_push_buffer(pd->dsrc,newDataFrame) ;
				g_print("Pushing data frame .. retval=%d buffers=%d vq=%d dq=%d\n",
						ret,++vbufsnt, g_queue_get_length(pd->videoframequeue.bufq), g_queue_get_length(pd->olddataqueue.bufq)) ;
			}
			/** Record the time **/
			{
				struct timeval prcTime ;
				struct timezone ntz;
				unsigned long tx_sec, tx_usec ;
				double tg_usec ;
				gettimeofday(&prcTime,&ntz) ;
				tx_usec = T.tstmp & 0xfffff ;
				tx_sec = (T.tstmp >> 20) & 0xfff ;
				prcTime.tv_sec &= 0xfff ;
				prcTime.tv_usec &= 0xfffff ;
				tg_usec = (double)((prcTime.tv_sec - tx_sec)%4096)*1000.0 + (double)(prcTime.tv_usec - tx_usec)/1000.0 ;
				if (tg_usec < 0) {
					g_print("Something wrong in time processing (%ld %ld proc) (%ld %ld current)\n",
							tx_sec, tx_usec, prcTime.tv_sec,prcTime.tv_usec) ;
				}
				else 
				{
					Dv->avgProcessTime += tg_usec ;
					g_print("Time processing (%ld %ld orig) (%ld %ld current)\n", tx_sec, tx_usec, prcTime.tv_sec,prcTime.tv_usec) ;
					g_print("Processing time for frame %d: %.4g msec: avg=%.4g\n",
							Dv->num_frames,tg_usec,Dv->avgProcessTime/Dv->num_frames) ;
				}
			}

			gst_buffer_unref(GST_BUFFER_CAST(videoFrameWaiting));
			gst_caps_unref(vcaps);
			gst_buffer_unref(GST_BUFFER_CAST(dataFrameWaiting));
		}
		/** Clean up the video frame queue **/
		dcvFtConnStatus(pd->ftc,pd->eos[EOS_USRC],pd->eos[EOS_USINK], pd->eosSent[EOS_USINK]) ;
		dcvAppSrcStatus(pd->usrc,&pd->usrcstate) ;
		g_print("\n") ;
		return 1 ;
	}
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
