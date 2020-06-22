#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include <plugins/elements/gsttee.h>

#include "gsftc.hpp"
#include "gutils.hpp"
#include "dsopencv.hpp"
static void help(char *name)
{
	g_print("Usage: %s -f <input file, webm format> | -n <recv port number> -p <port num for tx: default 50018> -i <dest ip address for transmisison> -l (local display) -w (to wait for signal)\n",name) ;  
}

static void processbuffer(void *A, int isz, void *B, int osz) ;
typedef struct {
	GstElement *pipeline;
	GstElement *tpt;
	GstElement *mdmx;
	GstElement *usink;
	GstAppSink *vsink;
	GstAppSrc  *dsrc;
	GstAppSrc  *vdisp;
	/* Output elements **/
	GstElement *mux;
	GstElement *op;
	GstElement *vp9d;
	srcstate_t dsrcstate;
	gboolean eos[MAX_EOS_TYPES];
	gboolean eosSent[MAX_EOS_TYPES];
	GstElement *fsrc ;
	unsigned long vsinkq;
	dcv_bufq_t dq;
	dcv_ftc_t *ftc;
} dpipe_t ;

int dcvFtcDebug=0;
#include <getopt.h>
#include "gutils.hpp"
#include "rseq.hpp"
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

int wait_for_signal = 1 ;

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;
extern void walkPipeline(GstBin *bin) ;

volatile gboolean terminate ;
volatile gboolean sigrcvd = FALSE ;
static char fdesc[] = "filesrc name=fsrc ! queue ! matroskademux name=mdmx ! tee name=tpoint \
			  rtpmux name=mux ! queue ! appsink name=usink \
			  tpoint.src_0 ! queue ! vp9dec name=vp9d ! videoconvert ! video/x-raw,format=BGR ! videoscale !  appsink name=vsink \
			  tpoint.src_1 ! queue ! rtpvp9pay name=vppy ! mux.sink_0 \
			  appsrc name=vdisp ! video/x-raw,height=480,width=848,format=BGR ! %s \
			  appsrc name=dsrc ! queue ! rtpgstpay name=rgpy ! mux.sink_1";
static char ndesc[] = "udpsrc name=usrc address=192.168.1.71 port=50017 ! queue ! application/x-rtp,media=video,clock-rate=90000,encoding-name=VP9 ! rtpvp9depay ! queue ! tee name=tpoint \
			  rtpmux name=mux ! queue ! appsink  name=usink \
			  tpoint.src_0 ! queue ! avdec_vp9 name=vp9d ! videoconvert ! video/x-raw,format=BGR ! videoscale ! appsink name=vsink \
			  tpoint.src_1 ! queue ! rtpvp9pay name=vppy ! mux.sink_0 \
			  appsrc name=vdisp ! video/x-raw,height=480,width=848,format=BGR ! %s \
			  appsrc name=dsrc ! queue ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";

std::string sdesc = "udpsrc name=usrc address= port=50017 ! queue ! application/x-rtp,media=video,clock-rate=90000,encoding-name=VP9 ! rtpvp9depay ! queue ! tee name=tpoint \
                          rtpmux name=mux ! queue ! appsink  name=usink \
                          tpoint.src_0 ! queue ! avdec_vp9 name=vp9d ! videoconvert ! video/x-raw,format=BGR ! videoscale ! appsink name=vsink \
                          tpoint.src_1 ! queue ! rtpvp9pay name=vppy ! mux.sink_0 \
                          appsrc name=vdisp ! video/x-raw,height=480,width=848,format=BGR ! %s \
                          appsrc name=dsrc ! queue ! application/x-rtp,media=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";

/*signal handler for edge node */
void handle_signal(int sig)
{
	g_print("sigusr received \n");
	wait_for_signal = 0;

}

char * addEdgeIpaddressTopdesc()
{
	char self_ip[20];
	memset(self_ip,"\0",20);
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
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);
	/*getting ip address of kni interface*/
	strcpy(self_ip,inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
	g_print("ip address=%s \n",self_ip);

	std::string s1;
	s1="address=";
	size_t pos1 = sdesc.find(s1);
	if (pos1 != std::string::npos)
		sdesc.insert(pos1 + s1.size(), self_ip);
	const char *srcfinal = sdesc.c_str();
	return srcfinal;

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
	gboolean waitflag=FALSE ;
	guint txport = 50018;
	guint rxport = 0;
	gboolean dotx = TRUE ;
	gboolean localdisplay = false ;
	gboolean selfip = false;
	char clientipaddr[1024];
	char videofile[1024] ; 
	static dcvFrameData_t Dv ;
	int numDataFrames=0;
	gboolean vdispEos = false ;
	strcpy(videofile,"v1.webm") ;
	strcpy(clientipaddr,"192.168.1.71") ;
	GstCaps *vcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "video", "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "VP9", NULL);
	GstCaps *dcaps = gst_caps_new_simple ("application/x-rtp", "media", G_TYPE_STRING, "application", "payload", G_TYPE_INT, 102, "clock-rate", G_TYPE_INT, 90000, "encoding-name", G_TYPE_STRING, "X-GST", NULL);
	static struct option longOpts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "localDisplay", required_argument, 0, 'l' },
		{ 0,0,0,0 }} ;
	int longindex;

	/*registering signal handler*/
        signal(SIGUSR1, handle_signal);

	while ((ch = getopt(argc, argv, "p:i:f:hn:lw")) != -1) {
		if (ch == 'p')
		{
			txport = atoi(optarg) ; g_print("Setting txport\n") ; 
		}
		if (ch == 'h') { help(argv[0]); exit(3) ; }
		if (ch == 'i') { strcpy(clientipaddr,optarg) ;  }
		if (ch == 'f') { strcpy(videofile, optarg) ;  }
		if (ch == 'n') { inputfromnet=TRUE; pdesc = ndesc ; rxport = atoi(optarg) ;  }
		if (ch == 'l') { localdisplay=TRUE;  }
		if (ch == 'w') { waitflag=TRUE;  }

	}

	/* check if application needs to wait for signal in case of intel edgenode*/
	if(TRUE == waitflag )
	{
		/*waiting for sigusr signal*/
		while(wait_for_signal)
		{
			g_print("waiting for signal .....\n");
			sleep(5);
		}
		/*calling function to add kni interface ip address in gst pipeline */
		pdesc=addEdgeIpaddressTopdesc();
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
	D.vp9d  = gst_bin_get_by_name(GST_BIN(D.pipeline),"vp9d") ;
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
	if (D.mdmx) g_signal_connect(D.mdmx, "pad-added", G_CALLBACK(muxpadAdded), D.tpt) ;

	if (inputfromnet == FALSE) {
		D.fsrc = gst_bin_get_by_name(GST_BIN(D.pipeline),"fsrc") ;
		g_assert(D.fsrc) ;
		g_object_set(G_OBJECT(D.fsrc), "location", videofile, NULL) ;
	}
	else {
		D.fsrc = gst_bin_get_by_name(GST_BIN(D.pipeline),"usrc") ;
		g_assert(D.fsrc) ;
		g_object_set(G_OBJECT(D.fsrc),"port", rxport, NULL) ; 
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
//			gst_pad_set_caps(rtpsink1, gst_caps_new_simple ("application/x-rtp", NULL)) ;
			g_print("rtpsink2 likes caps: %s\n", gst_caps_to_string(u)) ;
//			gst_pad_set_caps(rtpsink2,gst_caps_new_simple ("application/x-rtp", NULL)) ;
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
				if (v!=NULL) {
					databuf = dcvProcessStage(v,vcaps,NULL,&Dv,stage1,&newVideoFrame) ;

// Add a message dat	
					if (dotx && (databuf != NULL) )
					{
						GstFlowReturn ret = gst_app_src_push_buffer(D.dsrc,databuf) ;
						g_print("Pushing data buffer number %d...(ret=%d)...remaining(%u) status:dsrc=%d usink=%d vdisp=%d vsink=%d\n", 
								++numDataFrames, ret,g_queue_get_length(D.dq.bufq),D.eos[EOS_DSRC], D.eos[EOS_USINK], D.eos[EOS_VDISP], D.eos[EOS_VSINK]) ;
					}
					dcvBufContainerFree(dv) ;
					free(dv) ;
					dcvLocalDisplay(newVideoFrame,vcaps,D.vdisp,++Dv.num_frames) ;
				}
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
		if  (D.eos[EOS_VSINK] == true) {
			g_print("Received eos on vsink.") ;
			if (dcvIsDataBuffered(D.ftc) > 0) {
				g_print("Got pending data:%d\n", dcvBufferedBytes(D.ftc)) ;
			}
			g_print("Received eos on vsink... and all clear\n") ;
			if (D.dsrcstate.state != G_WAITING) continue ;
			else if (D.eos[EOS_USRC] == false && D.eosSent[EOS_USRC] == false)
			{
				gst_app_src_end_of_stream(D.dsrc) ;
				D.eosSent[EOS_USRC] = true ;
			}
			else
			{
				dcvFtConnClose(D.ftc) ;
				terminate = TRUE ;
			}
		}
	} while (terminate == FALSE || !g_queue_is_empty(D.dq.bufq)) ;
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
	g_print("Matching caps:%s to caps %s\n",gst_caps_to_string(t), gst_caps_to_string(u)) ;
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
