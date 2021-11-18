#include <stdio.h>
#include <ctype.h>

#include <unistd.h>
#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include <plugins/elements/gsttee.h>
#include "gsftc.hpp"
#include "rseq.h"
#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "gutils.hpp"
#include "gstdcv.h"
#include "gpipe.h"
#include "dsopencv.hpp"

GST_DEBUG_CATEGORY_STATIC (dscope_debug);
#define GST_CAT_DEFAULT dscope_debug

#define DATASRC udpsrc

int donothing(void *obj)
{
	GST_WARNING_OBJECT(GST_OBJECT(obj),"Doing Nothing\n") ;
}


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
	GstElement *dcv ;
	GstAppSink *usink;
	/** Input elements **/
	GstCaps *vcaps,*dcaps;
	/* Output elements **/
	gboolean eos[MAX_EOS_TYPES];
	gboolean eosSent[MAX_EOS_TYPES];
	/** Data holding elements **/
	dcv_ftc_t *ftc;
	GstElement *gstq1 ;
	GstElement *videoq1 ;
} dpipe_t ;

#define MAX_DATA_BUF_ALLOWED 240

static void eosRcvd(GstAppSink *slf, gpointer D) ;
static GstCaps * rtpBinPtMap(GstElement *s, guint pt, gpointer d) ;
static int rtpBinPtChanged(GstElement *s, guint session, guint pt, gpointer d) ;
static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void paddEventAdded(GstElement *s, GstPad *p, gpointer d) ;
static void paddEventRemoved(GstElement *s, GstPad *p, gpointer d) ;
#include <getopt.h>

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;

gboolean terminate =FALSE;
gboolean sigrcvd = FALSE ;
static dcvFrameData_t Dv ;

//termdesc = dcvdecl + indesc + procdesc + fakesink  + termdesc for tpoint 1
static char termdesc[] = "\
tpoint.src_1 ! queue ! fakesink" ;

//relaydesc = indesc + procdesc + outdesc
char * relaydesc = outdesc ;


gboolean dcvQueuesLoaded(dpipe_t *pD, grcvr_mode_e grcvrMode) ;
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
	guint rxport = 50013;
	gboolean dumpPipe = FALSE ;
	gboolean stage1 = FALSE ;
	gboolean tx=TRUE ;
	gboolean localdisplay = false ;
	char srcdesc[8192] ;
	guint txport = 50020 ;
	char ipaddress[45] = "192.168.16.205" ;
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

	while ((ch = getopt_long(argc, argv, "r:hp:i:ld:",longOpts,&longindex)) != -1) {
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
	GST_DEBUG_CATEGORY_INIT (dscope_debug, "dcv", 0, "This is my very own");
	char outpdesc[8192] ;
	sprintf(outpdesc,"dcv name=dcvMod %s %s %s %s",
			indesc,
			procdesc,
			localdisplay == true ? "autovideosink":"fakesink",
			termdesc) ;

	printf("Pipeline:\n\ndcv name=dcvMod\n %s\n%s %s\n %s\n\n",
			indesc,
			procdesc,
			localdisplay == true ? "autovideosink":"fakesink",
			termdesc) ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc ;

	D.pipeline = gst_parse_launch(outpdesc, &gerr);	
	
	if (gerr != NULL) {
		g_print("Couldn't create pipeline:%s\n", gerr->message) ;
		g_error_free(gerr) ;
		exit(1) ;
	}
	gst_element_set_name(D.pipeline, "grcvr_pipeline") ;
	for (int i = 0; i<MAX_EOS_TYPES; i++)
	{
		D.eos[i] = FALSE ;
		D.eosSent[i] = FALSE ;
	}


	/** Configure the end-points **/

	gerr = NULL ;
	D.vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "pt", G_TYPE_INT, 96, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "H264", NULL);
	D.dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "pt", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	if (grcvrMode == GRCVR_INTERMEDIATE) {
		D.usink = GST_APP_SINK_CAST(gst_bin_get_by_name(GST_BIN(D.pipeline),"usink")) ;
		dcvConfigAppSink(D.usink,dcvAppSinkNewSample, D.ftc, dcvAppSinkNewPreroll, D.ftc,dcvEosRcvd, &D.ftc) ; 
	}
	/** Saving the depayloader pads **/
	if (configurePortsIndesc(rxport,ipaddress,GST_ELEMENT(D.pipeline)) != TRUE) {

		GST_ERROR_OBJECT(D.pipeline,"Couldn't configure inbound ports\n") ;
		exit(3) ;
	}
	{
		{
			GstElement *vsdepay = gst_bin_get_by_name( GST_BIN(D.pipeline),"vsd") ; 
			GstElement *rgpdepay = gst_bin_get_by_name( GST_BIN(D.pipeline),"rgpd") ; 
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
			GstElement *rtpdmx = gst_bin_get_by_name( GST_BIN(D.pipeline),"rtpgst0") ;
			D.gstq1 = gst_bin_get_by_name(GST_BIN(D.pipeline),"gstq1") ;
			D.videoq1 = gst_bin_get_by_name(GST_BIN(D.pipeline),"videoq1") ;
			if (rtpdmx != NULL) {
				g_signal_connect(rtpdmx, "request-pt-map", G_CALLBACK(rtpBinPtMap), &D) ;
				g_signal_connect(rtpdmx, "pad-added", G_CALLBACK(paddEventAdded), &D) ;
				g_signal_connect(rtpdmx, "pad-removed", G_CALLBACK(paddEventRemoved), &D) ;
			}
			rtpdmx = gst_bin_get_by_name( GST_BIN(D.pipeline),"rtpvid1") ;
			if (rtpdmx != NULL) {
				g_signal_connect(rtpdmx, "request-pt-map", G_CALLBACK(rtpBinPtMap), &D) ;
				g_signal_connect(rtpdmx, "pad-added", G_CALLBACK(paddEventAdded), &D) ;
				g_signal_connect(rtpdmx, "pad-removed", G_CALLBACK(paddEventRemoved), &D) ;
			}
		}
		{
			
			GValue valueFn = { 0 } ;
			GValue valueMode = { 0 } ;
			gst_dcv_stage_t F ;
			if (grcvrMode == GRCVR_LAST)
			{
				F.sf = stage2 ;
				D.dcv = gst_bin_get_by_name(GST_BIN(D.pipeline),"dcvMod") ;
			}
			else {
				F.sf = stagen ;
				D.dcv = gst_bin_get_by_name(GST_BIN(D.pipeline),"dcvMod") ;
			}
			g_print("Setting execution function for %s\n",gst_element_get_name(D.dcv)) ;
			g_value_init(&valueFn,G_TYPE_POINTER) ;
			g_value_set_pointer(&valueFn,gpointer(&F)) ;
			g_object_set(G_OBJECT(D.dcv),"stage-function",gpointer(&F),NULL);
			g_value_init(&valueMode,G_TYPE_INT) ;
			g_value_set_int(&valueMode,grcvrMode) ;
			g_object_set(G_OBJECT(D.dcv),"grcvrMode",GRCVR_LAST,NULL);
			g_object_set(G_OBJECT(D.dcv),"eosFwd",FALSE,NULL);
		}
	}


	ret = gst_element_set_state (D.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (D.pipeline);
		return -1;
	}

	if ((grcvrMode != GRCVR_LAST))
	{	
		if ( (ret = gst_element_set_state(GST_ELEMENT_CAST(D.usink),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE )
			g_print("Couldn't set usink state to playing\n") ;
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
	} while (terminate == FALSE) ;

	{
		guint currentBytes,currentTime ;
		g_object_get(gst_bin_get_by_name(GST_BIN(D.pipeline),"gstq1"),
				"current-level-bytes", &currentBytes,
				"current-level-time", &currentTime, NULL) ;
		g_print("GST QUEUE 1: Backlog  %u, %u\n",currentBytes,currentTime) ;
	}
	g_print("Exiting!\n") ;
}


int dcvPushToSink(dpipe_t *pD)
{
	GstFlowReturn vret,dret;
	int ret=0; 
#if 0
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
#endif
	return ret;
}


/** Handlers **/

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D)
{
	GstPad *sinkpad;
	GstElement *dec = (GstElement *)D;
	g_print("muxpadAdded:Received pad added signal\n") ;
	sinkpad = gst_element_get_static_pad(dec ,"sink") ;
	gst_pad_link(p,sinkpad) ;
	gst_object_unref(sinkpad);
}

/** demux pad handlers **/
static void paddEventAdded(GstElement *g, GstPad *p, gpointer d)
{
	GstPad *peer ;
	dpipe_t *dp = (dpipe_t *)d;
	peer = gst_pad_get_peer(p) ;
	gchar *padname = GST_PAD_NAME(p) ;
	GST_OBJECT_LOCK(GST_OBJECT(g)) ;
	g_print("%s pad added\n",padname) ;
	if (peer) 
		g_print("%s connected to %s\n",GST_PAD_NAME(p), GST_PAD_NAME(gst_pad_get_peer(p))) ;
	else 
	{
		guint s1,session,pt;
		GstPadLinkReturn padret;
		sscanf(padname,"recv_rtp_src_%u_%u_%u",&s1,&session,&pt) ;
		if (pt == 102)
			peer = gst_element_get_static_pad(dp->gstq1,"sink") ;
		else
			peer = gst_element_get_static_pad(dp->videoq1,"sink") ;
		if (gst_pad_is_linked(peer))
			g_print("peer is already linked to %s\n",GST_PAD_NAME(gst_pad_get_peer(peer))) ;
		else if ( (padret = gst_pad_link(p,peer)) != GST_PAD_LINK_OK)
			g_print("Linking failed! retval=%d\n",padret) ;
		else
			g_print("Linked pad %s to %s\n", padname, GST_PAD_NAME(peer)) ;
	}
	GST_OBJECT_UNLOCK(GST_OBJECT(g)) ;
}

static void paddEventRemoved(GstElement *g, GstPad *p, gpointer d)
{
	g_print("%s pad removed\n",GST_PAD_NAME(p)) ;
}

static int rtpBinPtChanged(GstElement *s, guint session, guint pt, gpointer d)
{
	g_print("Session %u pt changed to %d\n",session,pt) ;
}

static GstCaps * rtpBinPtMap(GstElement *s, guint pt, gpointer d)
{
	static char vpn[] = "sinkv" ;
	static char dpn[] = "sinkd" ;
	char *nm;
	dpipe_t *dp = (dpipe_t *)d;
	GstPad *P = gst_element_get_static_pad(s,"recv_rtp_src") ; 
	GST_OBJECT_LOCK(GST_OBJECT(s)) ;
#if 0
	GstPad *vpd = dp->vp9extpad;
	GstPad *gpd = dp->gstextpad;
	GstPad *tpd = (pt == 102  ? gpd:vpd) ;
#endif
	GstCaps *tcps = (pt == 102 ? dp->dcaps:dp->vcaps) ;
	nm = (pt == 102 ? dpn:vpn) ;
	g_print("BinMap: Received pad added signal for pt=%d\n",pt) ;
	g_print("Returning caps:%s\n",gst_caps_to_string(tcps)) ;
	if (P==NULL) 
		g_print("Pad is null\n") ;
	else if (GST_PAD_IS_LINKED(P)) {
		GstPad *pp  ;
		while (P && GST_PAD_IS_LINKED(P))
		{
			P = gst_pad_get_peer(P) ;
			GstCaps *peercaps = gst_pad_query_caps(P,NULL) ;
			g_print("Pad linked!:to %s of %s, caps=%s\n", 
				GST_PAD_NAME(P), 
				GST_ELEMENT_NAME(gst_pad_get_parent_element(P)),
				gst_caps_to_string(peercaps)) ;
			P = gst_element_get_static_pad(gst_pad_get_parent_element(P),"src") ;
		}	 
	}
	else {
		g_print("Pad not linked\n") ;
	}
#if 0
	if (GST_PAD_IS_LINKED(tpd)) {
		g_print("SinkPad already linked!\n") ;
	}
	else gst_pad_link(P,tpd) ;
#endif
	GST_OBJECT_UNLOCK(GST_OBJECT(s)) ;
	return tcps ;
}

static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d)
{
	g_print("Received pad removed signal for pt=%d, %s\n",pt, GST_PAD_NAME(P)) ;
}
