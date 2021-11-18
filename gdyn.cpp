#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>

#include <gst/gst.h>
#include <gst/gstdebugutils.h>
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
#if 1
GST_DEBUG_CATEGORY_STATIC (dscope_debug);
#define GST_CAT_DEFAULT dscope_debug
#endif
#define DATASINK udpsink
static void help(char *name)
{
	g_print("Usage: %s -f <input file, mp4 format> | \
		-n <recv port number> \
		-p <tx port number starts from (four required)> \
		-i <dest ip address for transmission>\n\
		       	-l (local display) -e|--intel-edge --graphdump <graphdumpfile> \n",name) ;  
}

int donothing(void * obj)
{
	GST_WARNING_OBJECT(GST_OBJECT(obj),"Doing Nothing\n") ;
	return 0;
}

static void processbuffer(void *A, int isz, void *B, int osz) ;
typedef struct {
	GstElement *pipeline;
	GstElement *tpt;
	GstElement *vparse;
	GstElement *mdmx;
	GstElement *dcv ;
	/* Output elements **/
	GstElement *mux;
	GstElement *op;
	GstElement *vsd;
	GstElement *rtpvsdp;
	gboolean eos[MAX_EOS_TYPES];
	gboolean eosSent[MAX_EOS_TYPES];
	GstElement *fsrc ;
	unsigned long vsinkq;
	srcstate_t dsrcstate ;
	dcv_bufq_t dq;
} dpipe_t ;

extern int dcvFtcDebug;
extern int dcvGstDebug;
#include <getopt.h>
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

int wait_for_signal = 1 ;

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;
static GstCaps * rtpbinPtAdded(GstElement *rbin, guint ssrc, guint pt, gpointer d) ;
static void rtpbinPadAdded(GstElement *s, GstPad *p, gpointer *D) ;

volatile gboolean terminate ;
volatile gboolean sigrcvd = FALSE ;

//full pipe = fdesc + procdesc + outdesc
static char fdesc[] = "filesrc name=fsrc ! queue name=fq ! matroskademux name=mdmx ! parsebin name=vparse ! tee name=tpoint " ;
	
//full pipe = ndesc + procdesc + outdesc
static char ndesc[] = "rtpbin name=rbin \
		       udpsrc name=usrc address=192.168.16.205 port=50017 ! rbin.recv_rtp_sink_0 \
		       rbin.send_rtp_src_0 ! rtph264depay name=rtpvsdp ! queue %s ! tee name=tpoint " ;


#if 0
static char ndesc[] = "rtpbin name=rbin \
		       r3psrc name=usrc address=192.168.1.71 port=50017 ! rbin.recv_rtp_sink_0 \
		       rtph264depay name=rtpvsdp ! queue %s ! tee name=tpoint \
			dcv name=dcvMod \
			  rtpmux name=mux ! queue ! DATASINK timeout=500 \
			  tpoint.src_0 ! queue ! parsebin ! avdec_h264 name=vsd ! videoconvert ! video/x-raw,format=BGR ! videoscale ! dcvMod.video_sink \
			  tpoint.src_1 ! queue ! parsebin ! rtph264pay name=vppy ! mux.sink_0 \
			  dcvMod.video_src ! video/x-raw,format=BGR ! %s \
			  dcvMod.rtp_src ! queue name=dsq ! application/x-rtp,media=application,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";
#endif

/*search if  ip address is assigned to the kni interface*/

char * findIpaddress()
{
	char * self_ip=NULL;
	struct if_nameindex *if_nid, *intf;
	char if_name[20],*p;
	if_nid = if_nameindex();
	if ( if_nid != NULL )
	{
		for (intf = if_nid; intf->if_index != 0 || intf->if_name != NULL; intf++)
		{
			p=strstr(intf->if_name,"vEth");
			if(p)
			{       /*getting name of kni interface */
				strcpy(if_name,intf->if_name);
			}
		}
		if_freenameindex(if_nid);
	}
	g_print("kni interface %s \n",if_name);

	int fd;
	struct ifreq ifr;
	char int_name[20];

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, if_name, IFNAMSIZ-1);
	if(ioctl(fd, SIOCGIFADDR, &ifr) >=0)
	{
		self_ip = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
		g_print("ip address=%s \n",self_ip);
		close(fd);
		return self_ip;
	}
	else
		close(fd);
	return NULL;
}

int main( int argc, char** argv )
{

	dpipe_t D;
	GstStateChangeReturn ret;
	GError *gerr = NULL;
	char ch;
	extern char *optarg;
	static guint ctr=0;
	char outpipedesc[8192];
	gboolean inputfromnet=FALSE ;
	guint txport = 50013;
	guint rxport = 0;
	gboolean dotx = TRUE ;
	gboolean localdisplay = false ;
	gboolean selfip = false;
	char clientipaddr[1024];
	char videofile[1024] ; 
	static dcvFrameData_t Dv ;
	int numDataFrames=0;
	int eosstage = 0;
	gboolean vdispEos = false ;
	gboolean intel_platform= false;
	char graphfile[1024] ; 
	gboolean graphdump = false ;
	char qarg[1024] ;
	grcvr_mode_e grcvrMode = GRCVR_FIRST ;

	strcpy(videofile,"v1.webm") ;
	strcpy(clientipaddr,"192.168.16.205") ;
	sprintf(qarg,"") ;
	static struct option longOpts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "localDisplay", required_argument, 0, 'l' },
		{ "debug", required_argument, 0, 'd' },
		{ "intel-edge", no_argument, 0, 'e' },
		{ "queue", required_argument, 0, 'q' },
		{ "graphdump", required_argument, 0, 'G' },
		{ 0,0,0,0 }} ;
	int longindex;
	dcvFtcDebug=0 ;
	dcvGstDebug=0 ;
	char * ip_address=NULL;

	/*registering signal handler*/

	while ((ch = getopt_long(argc, argv, "p:i:f:hn:le",longOpts,&longindex)) != -1) {
		if (ch == 'p')
		{
			txport = atoi(optarg) ; g_print("Setting txport to %u\n",txport) ; 
		}
		if (ch == 'h') { help(argv[0]); exit(3) ; }
		if (ch == 'i') { strcpy(clientipaddr,optarg) ;  }
		if (ch == 'f') { strcpy(videofile, optarg) ;  }
		if (ch == 'n') { inputfromnet=TRUE; rxport = atoi(optarg) ;  }
		if (ch == 'l') { localdisplay=TRUE;  }
		if (ch == 'd') { dcvFtcDebug = atoi(optarg) & 0x03 ; dcvGstDebug = (atoi(optarg) >> 2) & 0x03 ;  }
		if (ch == 'e')
                {
			intel_platform=TRUE;
		}
		if (ch == 'G') {
			graphdump = true ;
			strncpy(graphfile,optarg,1023) ;
		}
		if (ch == 'q') {
			strcpy(qarg,optarg) ;
		}

	}
	/* check if application is running on  intel edgenode*/
	if(TRUE == intel_platform )
	{
		g_print("Intel Platform Flag set\n") ;
		/*waiting for ipaddress*/
		while(1)
		{
			ip_address=findIpaddress();
			if(ip_address!=NULL)
			{	
				g_print("ip addr is %s \n",ip_address);
				break;
			}
			g_print("waiting for ip address to be assigned to kni interface .....\n");
			sleep(5);
		}
	}

	gst_init(&argc, &argv) ;
	GST_DEBUG_CATEGORY_INIT (dscope_debug, "gdyn", 0, "This is my very own");

	g_print("Using txport = %u\n",txport) ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc, *srcpad ;

	
	sprintf(outpipedesc,"dcv name=dcvMod %s %s %s %s",
		inputfromnet == TRUE ? ndesc: fdesc,
		procdesc,
		localdisplay ? "autovideosink" : "fakesink",
		outdesc) ;
		
	printf("dcv name=dcvMod \n%s \n%s \n%s \n%s\n\n",
		inputfromnet == TRUE ? ndesc: fdesc,
		procdesc,
		localdisplay ? "autovideosink" : "fakesink",
		outdesc) ;
	
	gerr = NULL ;
	GError * error = NULL;
	D.pipeline = gst_parse_launch(outpipedesc,&error);
	if (error != NULL) {
		g_print("Couldn't create pipeline:%s\n", error->message) ;
		g_error_free(error) ;
		exit(1) ;
	}
	if (D.pipeline == NULL) {
		g_printerr("Couldn't create sub-bins\n") ;
		exit(4) ;
	}
	gst_element_set_name(D.pipeline, "gdyn_pipeline") ;
	D.tpt  = gst_bin_get_by_name(GST_BIN(D.pipeline),"tpoint") ;
	g_object_set(D.tpt,"silent",false, NULL) ;
	D.vsd  = gst_bin_get_by_name(GST_BIN(D.pipeline),"vsd") ;
	if (inputfromnet == FALSE) D.mdmx  = gst_bin_get_by_name(GST_BIN(D.pipeline),"mdmx") ;
	else D.mdmx = NULL ;
	for ( int i=0; i<MAX_EOS_TYPES; i++)
	{
		D.eos[i] = FALSE ;
		D.eosSent[i] = FALSE ;
	}
	D.vsinkq=0 ;
	dcvBufQInit(&D.dq) ;
	if (D.mdmx) 
	{
#if 1
		D.vparse = gst_bin_get_by_name(GST_BIN(D.pipeline),"vparse") ;
		g_assert(D.vparse) ;
		g_signal_connect(D.mdmx, "pad-added", G_CALLBACK(muxpadAdded), D.vparse) ;
#else
		g_signal_connect(D.mdmx, "pad-added", G_CALLBACK(muxpadAdded), D.tpt) ;
#endif
	}

	if (inputfromnet == FALSE) {
		D.fsrc = gst_bin_get_by_name(GST_BIN(D.pipeline),"fsrc") ;
		g_assert(D.fsrc) ;
		g_object_set(G_OBJECT(D.fsrc), "location", videofile, NULL) ;
		g_print("Setting file source to %s\n",videofile) ;
	}
	else {
		GstPad *srcpad, *sinkpad ;
		D.fsrc = gst_bin_get_by_name(GST_BIN(D.pipeline),"usrc") ;
		g_assert(D.fsrc) ;
		g_object_set(G_OBJECT(D.fsrc),"port", rxport, NULL) ; 
		if (ip_address != NULL)
			g_object_set(G_OBJECT(D.fsrc),"address", ip_address, NULL) ; 
		D.rtpvsdp = gst_bin_get_by_name(GST_BIN(D.pipeline),"rtpvsdp") ;
		g_assert(D.rtpvsdp) ;
		GstElement *rbin = gst_bin_get_by_name(GST_BIN(D.pipeline),"rbin") ;
		g_assert(rbin) ;
		g_object_set(G_OBJECT(rbin),"ignore-pt",false,NULL) ;
		g_signal_connect(rbin, "request-pt-map", G_CALLBACK(rtpbinPtAdded),NULL) ;
		g_signal_connect(rbin, "pad-added", G_CALLBACK(rtpbinPadAdded),&D) ;

	}

	if (configurePortsOutdesc(txport, clientipaddr, GST_ELEMENT(D.pipeline)) == false)
	{
		GST_ERROR_OBJECT(D.pipeline,"Couldn't configure outbound ports\n") ;
		exit(3) ;
	}
	g_object_set(G_OBJECT(D.tpt), "pull-mode", GST_TEE_PULL_MODE_SINGLE, NULL) ;
	{
		GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"vppy") ; g_assert(ge) ;
		g_object_set(G_OBJECT(ge), "pt", 96 ,NULL) ;
	}
	{
		GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"rgpy") ; g_assert(ge) ;
		mqsrc = gst_element_get_static_pad(ge, "src") ;
		GstCaps *t = gst_pad_query_caps(mqsrc,NULL) ;
		g_print("Rtp GST Pay wants %s caps \n", gst_caps_to_string(t)) ;
		g_object_set(G_OBJECT(ge), "pt", 102 ,NULL) ;
	}
#if 0
#endif
	{
		GValue valueFn = { 0 } ;
		GValue valueMode = { 0 } ;
		gst_dcv_stage_t F ;
		F.sf = stage1 ;
		D.dcv = gst_bin_get_by_name(GST_BIN(D.pipeline),"dcvMod") ;
		g_print("Setting execution function for %s\n",gst_element_get_name(D.dcv)) ;
		g_value_init(&valueFn,G_TYPE_POINTER) ;
		g_value_set_pointer(&valueFn,gpointer(&F)) ;
		g_object_set(G_OBJECT(D.dcv),"stage-function",gpointer(&F),NULL);
		g_value_init(&valueMode,G_TYPE_INT) ;
		g_value_set_int(&valueMode,grcvrMode) ;
		g_object_set(G_OBJECT(D.dcv),"grcvrMode",GRCVR_FIRST,NULL);
		g_object_set(G_OBJECT(D.dcv),"eosFwd",TRUE,NULL) ;
	}
	
	// Now link the pads
	if (!mqsrc) {
		g_print("Couldn't get static pad for message \n") ;
	}
	else if (gst_pad_is_linked(mqsrc)){
			g_print("%s:%s (mqsrc) linked to %s:%s\n",
					GST_ELEMENT_NAME(gst_pad_get_parent_element(mqsrc)),GST_PAD_NAME(mqsrc),
					GST_ELEMENT_NAME(gst_pad_get_parent_element(gst_pad_get_peer(mqsrc))),GST_PAD_NAME(gst_pad_get_peer(mqsrc))) ;
	}
	else {
		g_print ("mqsrc should have been linked!!!\n") ;
	}

	ret = gst_element_set_state (D.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (D.pipeline);
		return -1;
	}
#if 0
	if (D.dsrc) {
	ret = gst_element_set_state(GST_ELEMENT_CAST(D.dsrc),GST_STATE_PLAYING) ;
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("Unable to set data src to the playing state.\n");
		return -1;
	}
	}
	if (D.vsink) {
	if ( ( ret = gst_element_set_state(GST_ELEMENT_CAST(D.vsink),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE)
	{
		g_print("Couldn't set vsink state to playing\n") ;
		return -1;
	}
	}

	if (D.vdisp) {
	if ( ( ret = gst_element_set_state(GST_ELEMENT_CAST(D.vdisp),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE)
	{
		g_print("Couldn't set vdisp state to playing\n") ;
		return -1;
	}
	}
#endif

	GstState oldstate,newstate=GST_STATE_NULL ;
	terminate = FALSE ;
	do {
		GstBuffer *databuf ;
		GstBuffer *v = NULL ;
		GstMemory *vmem,*dmem;
		GstSegment *dbseg = NULL ;
		static guint vbufsnt = 0;
		static guint notprocessed = 6 ;

		if (terminate == FALSE) 
			terminate = listenToBus(D.pipeline,&newstate,&oldstate,50) ;

#if 0
		ctr++ ;
		if (ctr == 40) {
			guint currentBytes,currentTime ;
			gchar *lmsg;
			GST_WARNING("D.dsrcstate.state == %s, dq status = %s\n",
				D.dsrcstate.state == G_WAITING? "wait":"empty",
				g_queue_is_empty(D.dq.bufq) ? "empty" : "full") ;
			g_object_get(gst_bin_get_by_name(GST_BIN(D.pipeline),"fq"),
					"current-level-bytes", &currentBytes,
					"current-level-time", &currentTime, NULL) ;
			GST_WARNING("FQ: Backlog  %u, %u\n",currentBytes,currentTime) ;
			g_object_get(gst_bin_get_by_name(GST_BIN(D.pipeline),"ddq"),
					"current-level-bytes", &currentBytes,
					"current-level-time", &currentTime, NULL) ;
			GST_WARNING("T1: Backlog  %u, %u\n",currentBytes,currentTime) ;
			g_object_get(gst_bin_get_by_name(GST_BIN(D.pipeline),"vsq"),
					"current-level-bytes", &currentBytes,
					"current-level-time", &currentTime, NULL) ;
			GST_WARNING("T2: Backlog  %u, %u\n",currentBytes,currentTime) ;
			g_object_get(gst_bin_get_by_name(GST_BIN(D.pipeline),"dsq"),
					"current-level-bytes", &currentBytes,
					"current-level-time", &currentTime, NULL) ;
			GST_WARNING("DSQ: Backlog  %u, %u\n",currentBytes,currentTime) ;
			g_object_get(gst_bin_get_by_name(GST_BIN(D.pipeline),"tpoint"),
					"last-message", &lmsg, NULL) ;
			GST_WARNING("Tpoint: Last message %s\n",lmsg) ;
			ctr = 35;
		}
			
#endif
		if (newstate >= GST_STATE_READY) {
#if 0
#endif
			if (++notprocessed == 5) {
				if (dcvGstDebug & 0x02 == 0x02) 
					g_print("newstate=%d dsrcstate = %d queue=%d",newstate,D.dsrcstate.state,g_queue_get_length(D.dq.bufq)) ;
				notprocessed = 0 ;
			}

		}
#if 0
			if (++notprocessed == 5) {
				if (dcvGstDebug & 0x02 == 0x02) g_print("External:newstate=%d dsrcstate = %d queue=%d",newstate,D.dsrcstate.state,g_queue_get_length(D.dq.bufq)) ;
				notprocessed = 0 ;
			}
#endif
		if  (D.eos[EOS_VSINK] == true) {
			if (eosstage == 0)
				g_print("Received eos on vsink.newstate=%d dsrcstate = %d queue=%d",newstate,D.dsrcstate.state,g_queue_get_length(D.dq.bufq)) ;
			else if (eosstage < 1)
			{
				eosstage++ ;
				g_print("Received eos on vsink... and all clear\n") ;
				dcvFtcDebug = 3 ;
			}
			if (D.dsrcstate.state != G_WAITING) continue ;
			else if (D.eos[EOS_USRC] == false && D.eosSent[EOS_USRC] == false)
			{
				//gst_app_src_end_of_stream(D.dsrc) ;
				D.eosSent[EOS_USRC] = true ;
				eosstage++ ;
			}
			else
			{
			}
		}
	} while (terminate == FALSE || !g_queue_is_empty(D.dq.bufq)) ;
	g_print("Closing time..........") ;
	g_print(".....over\n") ;
}


static GstCaps * rtpbinPtAdded(GstElement *rbin, guint ssrc, guint pt, gpointer d)
{
	g_print("Pt added for ssrc%d pt%d\n",ssrc,pt) ;
	GstCaps *ptcaps = gst_caps_new_simple ("application/x-rtp", 
			"media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, 
			"encoding-name", G_TYPE_STRING, "H264", NULL);
	return  ptcaps ;
}
static void rtpbinPadAdded(GstElement *s, GstPad *p, gpointer *D)
{
	g_print("rtpbin added pad %s\n",GST_PAD_NAME(p)) ;
	GstPad *peer = gst_pad_get_peer(p) ;
	if (peer) {
		g_print("peer pad %s:%s\n",
				GST_ELEMENT_NAME(gst_pad_get_parent_element(peer)), GST_PAD_NAME(peer)) ;
	}
	else if (strncmp("recv_rtp_sink_",GST_PAD_NAME(p),strlen("recv_rtp_sink_")) == 0) {
		g_print("Unlinked rtp receive pad\n") ;
		dpipe_t *pD = (dpipe_t *)D ;
		peer = gst_element_get_static_pad(pD->fsrc,"src") ;
		g_assert(peer) ;
		if (gst_pad_link(peer,p) != GST_PAD_LINK_OK) {
			g_print ("Couldn't link pads\n") ;
		}
		else {
			g_print("%s:%s linked to %s:%s\n",
				GST_ELEMENT_NAME(gst_pad_get_parent_element(peer)), GST_PAD_NAME(peer), 
				GST_ELEMENT_NAME(gst_pad_get_parent_element(p)), GST_PAD_NAME(p)) ;
		}
	}
	else if (strncmp("recv_rtp_src_",GST_PAD_NAME(p),strlen("recv_rtp_src_")) == 0) {
		g_print("Unlinked rtp src pad\n") ;
		dpipe_t *pD = (dpipe_t *)D ;
		GstCaps * vcaps = gst_caps_new_simple (
				"application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "H264", NULL);
		gst_pad_set_caps(p,vcaps) ;
		peer = gst_element_get_static_pad(pD->rtpvsdp,"sink") ;
		g_assert(peer) ;
		GstPadLinkReturn ret = gst_pad_link(p,peer) ;
		if (GST_PAD_LINK_FAILED(ret)) {
			g_printerr("Couldn't link data to vsd: ret=%d\n", ret) ;
			if (ret == GST_PAD_LINK_WAS_LINKED) {
				GstPad * ppeer = gst_pad_get_peer(peer) ;
				g_print("Peer pad %s:%s is linked already to %s:%s\n",
						GST_ELEMENT_NAME(gst_pad_get_parent_element(peer)), GST_PAD_NAME(peer), 
						GST_ELEMENT_NAME(gst_pad_get_parent_element(ppeer)), GST_PAD_NAME(ppeer) ) ;
			}


		}
		else {
			g_print("%s:%s linked to %s:%s\n",
				GST_ELEMENT_NAME(gst_pad_get_parent_element(peer)), GST_PAD_NAME(peer), 
				GST_ELEMENT_NAME(gst_pad_get_parent_element(p)), GST_PAD_NAME(p)) ;
		}
	}

}

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D)
{
	GstPad *sinkpad;
	GstElement *dec = (GstElement *)D;
	GstCaps *t, *u ;
	g_print("Received pad added signal\n") ;
	sinkpad = gst_element_get_static_pad(dec ,"sink") ;
	t = gst_pad_query_caps(p,NULL) ;
	u = gst_pad_query_caps(sinkpad,NULL) ;
	g_print("Matching caps:%s \n\nto caps %s\n",gst_caps_to_string(t), gst_caps_to_string(u)) ;
	if (gst_pad_get_peer(p) != NULL) {
		g_print("%s:%s (p) linked to %s:%s\n",
				GST_ELEMENT_NAME(gst_pad_get_parent_element(p)),GST_PAD_NAME(p),
				GST_ELEMENT_NAME(gst_pad_get_parent_element(gst_pad_get_peer(p))),GST_PAD_NAME(gst_pad_get_peer(p))) ;
	}
	else {
		GstPadLinkReturn ret = gst_pad_link(p, sinkpad) ;
		if (GST_PAD_LINK_FAILED(ret)) {
			g_printerr("Couldn't link data to mux: ret=%d\n", ret) ;
			return ;
		}
	}
	gst_pad_set_active(p,TRUE) ;
	gst_object_unref(sinkpad);
}
