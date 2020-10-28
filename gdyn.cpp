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
#include "gstdcv.h"
#include "gutils.hpp"
#include "dsopencv.hpp"
static void help(char *name)
{
	g_print("Usage: %s -f <input file, mp4 format> | -n <recv port number> -p <port num for tx: default 50018> -i <dest ip address for transmisison>\n\
		       	-l (local display) -e|--intel-edge --graphdump <graphdumpfile> \n",name) ;  
}

static void processbuffer(void *A, int isz, void *B, int osz) ;
typedef struct {
	GstElement *pipeline;
	GstElement *tpt;
	GstElement *vparse;
	GstElement *mdmx;
	GstElement *usink;
	GstAppSink *vsink;
	GstAppSrc  *dsrc;
	GstAppSrc  *vdisp;
	/* Output elements **/
	GstElement *mux;
	GstElement *op;
	GstElement *vsd;
	GstElement *rtpvsdp;
	srcstate_t dsrcstate;
	gboolean eos[MAX_EOS_TYPES];
	gboolean eosSent[MAX_EOS_TYPES];
	GstElement *fsrc ;
	unsigned long vsinkq;
	dcv_bufq_t dq;
	dcv_ftc_t *ftc;
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
extern void walkPipeline(GstBin *bin) ;

volatile gboolean terminate ;
volatile gboolean sigrcvd = FALSE ;
static char fdesc[] = "filesrc name=fsrc ! queue ! matroskademux name=mdmx ! parsebin name=vparse ! tee name=tpoint \
			  rtpmux name=mux ! queue ! appsink name=usink \
			  tpoint.src_0 ! queue ! parsebin ! avdec_h264 name=vsd ! videoconvert ! video/x-raw,format=BGR ! videoscale !  appsink name=vsink \
			  tpoint.src_1 ! queue ! parsebin ! rtph264pay name=vppy ! mux.sink_0 \
			  appsrc name=vdisp ! video/x-raw,format=BGR ! %s \
			  appsrc name=dsrc ! queue ! rtpgstpay name=rgpy ! mux.sink_1";
static char ndesc[] = "rtpbin name=rbin \
		       udpsrc name=usrc address=192.168.1.71 port=50017 ! rbin.recv_rtp_sink_0 \
		       rtph264depay name=rtpvsdp ! queue ! tee name=tpoint \
			  rtpmux name=mux ! queue ! appsink  name=usink \
			  tpoint.src_0 ! queue ! parsebin ! avdec_h264 name=vsd ! videoconvert ! video/x-raw,format=BGR ! videoscale ! appsink name=vsink \
			  tpoint.src_1 ! queue ! parsebin ! rtph264pay name=vppy ! mux.sink_0 \
			  appsrc name=vdisp ! video/x-raw,format=BGR ! %s \
			  appsrc name=dsrc ! queue ! application/x-rtp,media=application,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";

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
	char pipedesc[8192];
	char *pdesc = fdesc;
	gboolean inputfromnet=FALSE ;
	guint txport = 50018;
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

	strcpy(videofile,"v1.webm") ;
	strcpy(clientipaddr,"192.168.1.71") ;
	static struct option longOpts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "localDisplay", required_argument, 0, 'l' },
		{ "debug", required_argument, 0, 'd' },
		{ "intel-edge", no_argument, 0, 'e' },
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
			txport = atoi(optarg) ; g_print("Setting txport\n") ; 
		}
		if (ch == 'h') { help(argv[0]); exit(3) ; }
		if (ch == 'i') { strcpy(clientipaddr,optarg) ;  }
		if (ch == 'f') { strcpy(videofile, optarg) ;  }
		if (ch == 'n') { inputfromnet=TRUE; pdesc = ndesc ; rxport = atoi(optarg) ;  }
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
	GST_DEBUG_CATEGORY_INIT (my_category, "gdyn", 0, "This is my very own");

	g_print("Using txport = %u\n",txport) ;
	
	GstPad *rtpsink1, *rtpsink2, *mqsrc, *srcpad ;

	
	if (localdisplay) {
		sprintf(pipedesc,pdesc,"autovideosink") ;
	}
	else {
		sprintf(pipedesc,pdesc,"fakesink") ;
	}

	
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
	D.tpt  = gst_bin_get_by_name(GST_BIN(D.pipeline),"tpoint") ;
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
	D.dsrcstate.state = G_BLOCKED;
	D.dsrcstate.length = 0;
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

	{
		g_print("Setting destination to %s:%u\n",clientipaddr,txport) ;
		D.usink = gst_bin_get_by_name(GST_BIN(D.pipeline),"usink") ;
		D.ftc = dcvFtConnInit(NULL,-1,clientipaddr,txport) ;
		if (D.ftc == NULL) {
			g_print("Something went wrong in initialization\n") ;
			exit(3) ;
		}
		dcvConfigAppSink(GST_APP_SINK_CAST(D.usink),dcvAppSinkNewSample, D.ftc, dcvAppSinkNewPreroll, D.ftc,eosRcvd, &D.eos[EOS_USINK]) ; 
	}
	{
		g_object_set(G_OBJECT(D.tpt), "pull-mode", GST_TEE_PULL_MODE_SINGLE, NULL) ;
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
		{
			GstElement * ge = gst_bin_get_by_name(GST_BIN(D.pipeline),"dsrc") ; g_assert(ge) ;
			D.dsrc = GST_APP_SRC_CAST(ge) ;
		  	GstCaps *caps = gst_caps_new_simple ("application/x-rtp",
		  		"media",G_TYPE_STRING,"application","clock-rate",G_TYPE_INT,90000,"payload",G_TYPE_INT,102,"encoding-name",G_TYPE_STRING,"X-GST",NULL) ;
			dcvConfigAppSrc(D.dsrc,dataFrameWrite,&D.dsrcstate,dataFrameStop,&D.dsrcstate,eosRcvdSrc, &D.eos[EOS_DSRC],caps) ;
		}
		{
			dcvConfigAppSink(D.vsink,sink_newsample, &D.dq, sink_newpreroll, &D.dq,eosRcvd, &D.eos) ; 
		}
		{
			D.vdisp = GST_APP_SRC_CAST(gst_bin_get_by_name( GST_BIN(D.pipeline), "vdisp")) ;
			g_assert(D.vdisp) ;
			GstCaps *srccaps = gst_caps_new_simple ( "video/x-raw", NULL ) ;
		 	dcvConfigAppSrc(D.vdisp, NULL , NULL, NULL , NULL, eosRcvdSrc, &D.eos[EOS_VDISP],srccaps) ;
		}
	}
	
	// Now link the pads
  	if (!rtpsink1 || !rtpsink2) { 
		  g_print("Couldn't get request pads\n") ; 
	}
#if VP9PAY
#endif
	else if (!mqsrc) {
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

	if (dcvFtConnStart(D.ftc) == FALSE) {
		g_print("Something happened during connection start\n") ;
		exit(3) ;
	}

	ret = gst_element_set_state (D.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (D.pipeline);
		return -1;
	}
	ret = gst_element_set_state(GST_ELEMENT_CAST(D.dsrc),GST_STATE_PLAYING) ;
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("Unable to set data src to the playing state.\n");
		return -1;
	}
	if ( ( ret = gst_element_set_state(GST_ELEMENT_CAST(D.vsink),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE)
	{
		g_print("Couldn't set vsink state to playing\n") ;
		return -1;
	}

	ret = gst_element_set_state(GST_ELEMENT_CAST(D.usink),GST_STATE_PLAYING) ;
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("Unable to set usink to the playing state.\n");
		return -1;
	}
	if ( ( ret = gst_element_set_state(GST_ELEMENT_CAST(D.vdisp),GST_STATE_PLAYING)) == GST_STATE_CHANGE_FAILURE)
	{
		g_print("Couldn't set vdisp state to playing\n") ;
		return -1;
	}

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
			terminate = listenToBus(D.pipeline,&newstate,&oldstate,20) ;

		ctr++ ;
		if (newstate >= GST_STATE_READY) {
			while (D.dsrcstate.state == G_WAITING && !g_queue_is_empty(D.dq.bufq)){
				dcv_BufContainer_t *dv ;
				ctr = 0 ;
				dv = (dcv_BufContainer_t *)g_queue_pop_head(D.dq.bufq) ;
				GstCaps *vcaps ;
				v = GST_BUFFER_CAST(dv->nb) ;
				vcaps = dv->caps;
				GstBuffer * newVideoFrame ;
				GstBuffer * databuf ;
				notprocessed = 0 ;
				if (v!=NULL) {
					databuf = dcvProcessStage(v,vcaps,NULL,&Dv,stage1,&newVideoFrame) ;

// Add a message dat	
					if ( dotx && (databuf != NULL) )
					{
						GstFlowReturn ret = gst_app_src_push_buffer(D.dsrc,databuf) ;
						g_print("Pushing data buffer number %d...(ret=%d)...remaining(%u) status:dsrc=%d usink=%d vdisp=%d vsink=%d\n", 
								++numDataFrames, ret,g_queue_get_length(D.dq.bufq),D.eos[EOS_DSRC], D.eos[EOS_USINK], D.eos[EOS_VDISP], D.eos[EOS_VSINK]) ;
						g_print("Bytes sent:%d\n", D.ftc->sentbytes) ;
					}
					dcvBufContainerFree(dv) ;
					free(dv) ;
					if (localdisplay) dcvLocalDisplay(newVideoFrame,vcaps,D.vdisp,++Dv.num_frames) ;
					else Dv.num_frames++ ;
					if (Dv.num_frames == 1 && graphdump == true) {
						g_print("Dumping bin to file %s\n",graphfile) ;
						GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(D.pipeline), GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE,graphfile) ;
					}

				}
			}
			if (++notprocessed == 5) {
				if (dcvGstDebug & 0x02 == 0x02) g_print("newstate=%d dsrcstate = %d queue=%d",newstate,D.dsrcstate.state,g_queue_get_length(D.dq.bufq)) ;
				notprocessed = 0 ;
			}

			while (D.ftc->totalbytes > D.ftc->spaceleft) {
				if (dcvFtcDebug) g_print("Pending data in input buffer! %d bytes\n",D.ftc->totalbytes - D.ftc->spaceleft) ;
				if (dcvPushBuffered(GST_APP_SRC_CAST(D.fsrc),D.ftc)  <= 0) break ;
			}
			if (D.ftc->totalbytes > D.ftc->spaceleft) { 
				if (dcvFtcDebug) g_print("%d bytes pending\n",(D.ftc->totalbytes - D.ftc->spaceleft)) ; 
			}
			else {
			}
		}
			if (++notprocessed == 5) {
				if (dcvGstDebug & 0x02 == 0x02) g_print("External:newstate=%d dsrcstate = %d queue=%d",newstate,D.dsrcstate.state,g_queue_get_length(D.dq.bufq)) ;
				notprocessed = 0 ;
			}
		if  (D.eos[EOS_VSINK] == true) {
			if (eosstage == 0)
				g_print("Received eos on vsink.newstate=%d dsrcstate = %d queue=%d",newstate,D.dsrcstate.state,g_queue_get_length(D.dq.bufq)) ;
			if (dcvIsDataBuffered(D.ftc) > 0) {
				g_print("Got pending data:%d\n", dcvBufferedBytes(D.ftc)) ;
				if (eosstage > 0) eosstage-- ;
			}
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
	dcvFtConnClose(D.ftc) ;
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
