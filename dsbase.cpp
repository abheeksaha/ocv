#include <stdio.h>
#include <ctype.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include <plugins/elements/gsttee.h>
#include "gutils.hpp"
#include "rseq.hpp"

#define MAX_STAY 1
static void help()
{
}

static void processbuffer(void *A, int isz, void *oB, int obsz, void *B, int bsz) ;
GstPadProbeReturn cb_have_data (GstPad  *pad, GstPadProbeInfo *info, gpointer user_data) ;
typedef struct {
	GstElement *pipeline;
	GstElement *vdec;
	GstAppSink *vsink;
	GstAppSink *dsink;
	/** Input elements **/
	GstCaps *vcaps,*dcaps;
	/* Output elements **/
	GstElement *mux;
	GstElement *op;
	GstAppSrc *dsrc;
	srcstate_e dsrcstate;
	gboolean deos;
	gboolean veos;
	/** Data holding elements **/
	dcv_bufq_t dataqueue;
	dcv_bufq_t olddataqueue;
	dcv_bufq_t videoframequeue;
} dpipe_t ;
#define MAX_DATA_BUF_ALLOWED 240

static void eosRcvd(GstAppSink *slf, gpointer D) ;
static void demuxpadAdded(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void demuxpadRemoved(GstElement *s, guint pt, GstPad *P, gpointer d) ;
static void paddEventAdded(GstElement *s, GstPad *p, gpointer d) ;
static void paddEventRemoved(GstElement *s, GstPad *p, gpointer d) ;
#include <getopt.h>

static void muxpadAdded(GstElement *s, GstPad *p, gpointer *D) ;

static char srcdesc[] = "\
udpsrc name=usrc address=192.168.1.71 port=50018 ! rtpptdemux name=rpdmx \
rpdmx.src_96 ! rtpvp9depay name=vp9d ! tee name=tpoint \
tpoint.src_0 ! queue ! avdec_vp9 name=vdec ! videoconvert ! videoscale ! tee name=tpoint2 \
	tpoint2.src_0 ! queue ! fakesink \
	tpoint2.src_1 ! queue ! appsink name=vsink \
rtpmux name=mux ! queue !  udpsink name=usink host=192.168.1.71 port=50019 \
rpdmx.src_102 ! rtpgstdepay name=rgpd ! appsink name=dsink \
tpoint.src_1 ! queue ! rtpvp9pay ! mux.sink_0 \
appsrc name=dsrc !  application/x-rtp,medial=application,clock-rate=90000,payload=102,encoding-name=X-GST ! rtpgstpay name=rgpy ! mux.sink_1";

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
	gboolean tx=TRUE ;
	help();
	guint ctr=0;

	while ((ch = getopt(argc, argv, "Np:ds:")) != -1) {
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
		if (ch == 'N') {
			tx = FALSE ;
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
	D.dataqueue.bufq = g_queue_new() ;
	D.olddataqueue.bufq = g_queue_new() ;
	D.videoframequeue.bufq = g_queue_new() ;
	D.deos = FALSE ;
	D.veos = FALSE ;
	D.dsrcstate = G_BLOCKED;

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
		}
		{
			dcvConfigAppSink(D.vsink,sink_newsample, &D.videoframequeue, sink_newpreroll, &D.videoframequeue,eosRcvd, &D.veos) ; 
			dcvConfigAppSink(D.dsink,sink_newsample, &D.olddataqueue, sink_newpreroll, &D.olddataqueue,eosRcvd, &D.deos) ; 
			dcvConfigAppSrc(D.dsrc,dataFrameWrite,&D.dsrcstate,dataFrameStop,&D.dsrcstate) ;
		}
	/** Saving the depayloader pads **/
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
#if 1
		{
			GstElement * tp = gst_bin_get_by_name ( GST_BIN(D.pipeline), "tpoint") ;
			if (tp) {
			g_object_set(G_OBJECT(tp),"has-chain",TRUE, NULL) ;
			g_object_set(G_OBJECT(tp),"allow-not-linked",TRUE, NULL) ;
			}
		}
#endif
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
		return (4) ;
	}

	gst_element_set_state(GST_ELEMENT_CAST(D.dsrc),GST_STATE_PLAYING) ;
	gst_element_set_state(GST_ELEMENT_CAST(D.vsink),GST_STATE_PLAYING) ;
	gst_element_set_state(GST_ELEMENT_CAST(D.dsink),GST_STATE_PLAYING) ;

	gboolean terminate = FALSE ;
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
		if (inputstate == GST_STATE_PLAYING || inputstate == GST_STATE_PAUSED ) {
			gpointer videoFrameWaiting = NULL ;
			gpointer dataFrameWaiting = NULL ;
			GstClockTime vframenum, vframeref;
			guint64 *pd;
			gint vfmatch;
			dcv_BufContainer_t *dataFrameContainer;

			while (!g_queue_is_empty(D.videoframequeue.bufq)&& !g_queue_is_empty(D.olddataqueue.bufq)) 
			{
				int stay=0;
				ctr = 0 ;
				if ( (dataFrameContainer = (dcv_BufContainer_t *)g_queue_pop_head(D.olddataqueue.bufq)) == NULL) {
				       g_print("No data frame ...very strange\n") ;
				}
				if ( ((vfmatch = dcvFindMatchingContainer(D.videoframequeue.bufq,dataFrameContainer)) == -1) &&
			       	      (vfmatch >= g_queue_get_length(D.videoframequeue.bufq))){
					g_print("No match found: vfmatch=%d\n",vfmatch) ;
					if ( (stay = dcvLengthOfStay(dataFrameContainer)) > MAX_STAY)  {
						dcvBufContainerFree(dataFrameContainer) ;
						free(dataFrameContainer) ;
						g_print("Dropping data buffer, no match for too long\n") ;
					}
					else 
						g_queue_push_tail(D.olddataqueue.bufq,dataFrameContainer) ;

					continue ;
				}
				else
				{
					dataFrameWaiting = dataFrameContainer->nb;
					videoFrameWaiting = ((dcv_BufContainer_t *)g_queue_pop_nth(D.videoframequeue.bufq,vfmatch))->nb ;
					GstMemory *vmem,*dmem,*odmem;
					GstMapInfo vmap,odmap,dmap;
					gboolean match=FALSE ;
					vmem = gst_buffer_get_all_memory(videoFrameWaiting) ;
					odmem = gst_buffer_get_all_memory(dataFrameWaiting) ;
					if (gst_memory_map(vmem, &vmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in vbuffer\n") ; }
					if (gst_memory_map(odmem, &odmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in dbuffer\n") ; }
					{
						GstBuffer *databuf = gst_buffer_new_allocate(NULL,getTagSize(),NULL) ;
						dmem = gst_buffer_get_all_memory(databuf) ;
						if (gst_memory_map(dmem, &dmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in dbuffer\n") ; }
						g_print("Input:%d,%d o/p:%d..\n",vmap.size,odmap.size, dmap.size) ;
						pd = (guint64 *)dmap.data;
						processbuffer(vmap.data,vmap.size,odmap.data,odmap.size,dmap.data,dmap.size) ;
						gst_memory_unmap(dmem,&dmap);

// Add a message dat
						g_print("Pushing data buffer...%u %u\n",
								g_queue_get_length(D.videoframequeue.bufq),
								g_queue_get_length(D.olddataqueue.bufq)) ;
						gst_app_src_push_buffer(D.dsrc,databuf) ;
					}
					gst_memory_unmap(vmem,&vmap) ;
					gst_memory_unmap(odmem,&odmap) ;
					gst_buffer_unref(videoFrameWaiting);
					gst_buffer_unref(dataFrameWaiting);
				}
			}
		}
		if (ctr == 5000)
			walkPipeline(D.pipeline,0) ;
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

GstPadProbeReturn cb_have_data (GstPad  *pad, GstPadProbeInfo *info, gpointer user_data)
{
  GstMapInfo map;
  guint16 *ptr, t;
  GstBuffer *buffer;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  dcv_bufq_t *dp = (dcv_bufq_t *)user_data ;
  GstBuffer * newbuf = gst_buffer_copy_deep(buffer) ;
  g_queue_push_tail(dp->bufq,gst_buffer_ref(newbuf)) ;
  return GST_PAD_PROBE_OK;
}
