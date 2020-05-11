#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include "gutils.hpp"
#include "rseq.hpp"
gboolean listenToBus(GstElement *pipeline, GstState * nstate, GstState *ostate, unsigned int tms)
{
	GstBus *bus;
	GstMessage *msg;
	GstState pstate;
	gboolean terminate = FALSE ;
	bus = gst_element_get_bus (pipeline);
	msg = gst_bus_timed_pop_filtered (bus, tms*GST_MSECOND,
		GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("%s:End-Of-Stream reached.\n",gst_element_get_name(pipeline));
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
            GstState pending_state;
            gst_message_parse_state_changed (msg, ostate, nstate, &pending_state);
            g_print ("Pipeline %s state changed from %s to %s:\n",
			    GST_ELEMENT_NAME(pipeline),
                gst_element_state_get_name (*ostate), gst_element_state_get_name (*nstate));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }


    return terminate;
}

void walkPipeline(GstElement *p, guint level)
{
	GstIterator * git;
	GValue gv = {0,};
	GstIteratorResult gres;
	GstElement * ele;
	GstState cstate,pstate ;
	GstBin *bin = GST_BIN(p) ;
	git = gst_bin_iterate_sorted(bin) ;
	while ( (gres = gst_iterator_next(git,&gv)) != GST_ITERATOR_ERROR) {
		if (gres == GST_ITERATOR_DONE) { g_print("]\n") ; break ; }
		else if (gres == GST_ITERATOR_RESYNC) { gst_iterator_resync(git) ; continue ; }
		GstElement *ele= GST_ELEMENT_CAST(g_value_get_object(&gv)) ;
		GstStateChangeReturn  eles = gst_element_get_state(ele, &cstate,&pstate, GST_SECOND) ;
		if (eles != GST_STATE_CHANGE_FAILURE) {
			gchar *gs = gst_element_state_get_name(cstate) ;
			g_print("Elem:%s state:%s Has clock:%s ",GST_ELEMENT_NAME(ele),gs,ele->clock == NULL? "no":"yes") ;
		}
		else {
			g_print("Elem:%s [No state change info] Has clock:%s ",GST_ELEMENT_NAME(ele),ele->clock == NULL? "no":"yes") ;
		}

		if (level > 0)
		{
			GList *gl;
			g_print("Num Src: %d Num Sink : %d\n",ele->numsrcpads, ele->numsinkpads) ;
			if (ele->numsrcpads > 0) g_print("\tSrcs\n") ;
			for (gl = g_list_first(ele->srcpads); gl; gl = gl->next ) {
				GstPad * srcp = GST_PAD_CAST(gl->data) ;
				gchar *nm ;
				GstCaps *cp;
				gboolean islinked = gst_pad_is_linked(srcp);
				cp = gst_pad_get_current_caps(srcp) ;
				gint64 offset = gst_pad_get_offset(srcp) ;
				g_print("\t\tname:%s caps=%s offset=%.4g linked=%s:%s\n",
						GST_PAD_NAME(srcp),
						cp != NULL ? gst_caps_to_string(cp): "no caps",
						(double)offset/1000000.0,
						islinked? GST_ELEMENT_NAME(gst_pad_get_parent(gst_pad_get_peer(srcp))) : "none",
						islinked? GST_PAD_NAME(gst_pad_get_peer(srcp)) : "none") ;
			}
			if (ele->numsinkpads > 0) g_print("\tSinks\n") ;
			for (gl = g_list_first(ele->sinkpads); gl; gl = gl->next ) {
				GstPad * sinkp = GST_PAD_CAST(gl->data) ;
				gchar *nm ;
				GstCaps *cp;
				gboolean islinked = gst_pad_is_linked(sinkp);
				cp = gst_pad_get_current_caps(sinkp) ;
				gint64 offset = gst_pad_get_offset(sinkp) ;
				g_print("\t\tname:%s caps=%s offset=%.4g linked=%s:%s\n",
						GST_PAD_NAME(sinkp),
						cp != NULL ? gst_caps_to_string(cp): "no caps",
						(double)offset/1000000.0,
						islinked? GST_ELEMENT_NAME(gst_pad_get_parent(gst_pad_get_peer(sinkp))) : "none",
						islinked? GST_PAD_NAME(gst_pad_get_peer(sinkp)) : "none") ;
			}
		}
	}
	if (gres == GST_ITERATOR_ERROR) { g_print("Unrecoverable error]\n") ; }
	return ;
}


#include <sys/time.h>
void tagbuffer(void *A, int isz, void *B, int osz)
{
	int i;
	static int count=0;
	struct timeval Tv;
	struct timezone tz;
	u32 *pA = (u32 *)A ;
	tag_t *pd = (tag_t *)B ;
	pd->count = count++ ;
	gettimeofday(&Tv,&tz) ;
	pd->tstmp = Tv.tv_usec % (1<<30)  ;
	pd->seqsize =  RSEQSIZE ;
	for (i=0; i<RSEQSIZE; i++) 
		pd->seq[i] = rseq[i] ;
	pd->checksum=0;
	/* Now write the checksum at the end */
	g_print("Tag:count=%u tstmp=%u Sequence of size %u ", pd->count,pd->tstmp, RSEQSIZE) ;
	for (i=0; i<RSEQSIZE; i++)
	{
		if (rseq[i] >= isz/sizeof(u32)) {
			g_print("%u is too big for %u \n",
				rseq[i], isz) ;
			g_assert(rseq[i] < (isz/sizeof(u32))) ;
		}
		g_print("pA[%u]=%u ",rseq[i],pA[rseq[i]]) ;
		pd->checksum ^= pA[rseq[i]] ;
	}
	g_print("Final hash for sequence size %u is %u\n",RSEQSIZE, pd->checksum) ;
}


gboolean matchbuffer(void *A, int isz, void *B, int osz)
{
	gboolean retval = FALSE ;
	int i;
	u32 *pA = (u32 *)A ;
	tag_t *pd = (tag_t *)B ;
	u32 hsh=0;
	int sequence[1024];
	g_assert(pd->seqsize < 1024) ;
	g_print("Seq of size %d: count(%u) tstmp=%u hash=%u: ", pd->seqsize,pd->count,pd->tstmp,pd->checksum)  ;
	g_assert(pd->seqsize == RSEQSIZE) ;
	for (i=0; i<pd->seqsize; i++) {
		sequence[i] = pd->seq[i] ;
		g_print("pA[%u]=%u ",sequence[i],pA[sequence[i]]) ;
		hsh ^= pA[sequence[i]] ;
	}
	g_print("Checksum = %u ..", hsh) ;
	hsh ^= pd->checksum ;
	if (hsh == 0) { retval = TRUE ; }
	else retval = FALSE ;
	g_print("hsh = %u (%s)\n", hsh, (hsh == 0? "match":"fail")) ;
	return retval ;
}

int getTagSize()
{
	int sz = sizeof(tag_t) ;
	return sz ;
}

void eosRcvd(GstAppSink *slf, gpointer D)
{
	g_print("Eos on %s\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(slf))) ;
	gboolean *eos = (gboolean *)D ;
	*eos = TRUE ;
}

void testStats(GstElement *tst)
{
	GstStructure *statsStruct = gst_structure_new_empty("sinkstats");
	g_assert(tst != NULL) ;
	g_object_get(tst, "stats", statsStruct,NULL) ;
	g_print("Stats for testsink:%s\n", gst_structure_to_string(statsStruct)) ;
	gst_structure_free(statsStruct) ;
	return ;
}
GstFlowReturn sink_newpreroll(GstAppSink *slf, gpointer d)
{
	GstCaps * dbt = NULL ;
	gchar *gs;
	if (d != NULL) {
		dcv_bufq_t *D = (dcv_bufq_t *)d ;
		GstSample *gsm ;
		if ((gsm = gst_app_sink_pull_preroll(slf)) != NULL)
		{
			g_print("New Preroll in %s: %u --",GST_ELEMENT_NAME(slf),g_queue_get_length(D->bufq)) ;
			dbt = gst_sample_get_caps(gsm) ;
			gs = gst_caps_to_string(dbt);
			g_print("Data Received: caps %s \n",gs) ;
			g_free(gs) ;
			gst_sample_unref(gsm) ;
			gst_caps_unref(dbt) ;
			return GST_FLOW_OK;
		}
		else
			return GST_FLOW_ERROR;
	}
	else 
		g_print("New Preroll in %s:\n",GST_ELEMENT_NAME(slf)) ;
}

GstFlowReturn sink_newsample(GstAppSink *slf, gpointer d)
{
	int cnt=0;
	if (d != NULL) {
		dcv_bufq_t *D = (dcv_bufq_t *)d ;
		GstSample *gsm ;
		if ((gsm = gst_app_sink_pull_sample(slf)) != NULL) {
			GstBuffer *gb = gst_sample_get_buffer(gsm);
			GstBuffer *nb = gst_buffer_copy_deep(gb);
			g_queue_push_tail(D->bufq,gst_buffer_ref(nb)) ;
			g_print("New Sample in %s: %u\n",GST_ELEMENT_NAME(slf),g_queue_get_length(D->bufq)) ;
			return GST_FLOW_OK;
		}
		else
			return GST_FLOW_ERROR ;
	}
	else 
		g_print("New Sample in %s:\n",GST_ELEMENT_NAME(slf)) ;
}

gboolean dcvConfigAppSrc(GstAppSrc *dsrc, src_dfw_fn_t src_dfw, void *dfw, src_dfs_fn_t src_dfs, void *dfs )
{
	if (dsrc == NULL) return FALSE ;
	g_object_set(G_OBJECT(dsrc), "format", GST_FORMAT_TIME,NULL) ;
	g_object_set(G_OBJECT(dsrc), "stream-type", GST_APP_STREAM_TYPE_STREAM,NULL) ;
	g_object_set(G_OBJECT(dsrc), "emit-signals", TRUE,NULL) ;
	g_object_set(G_OBJECT(dsrc), "block", TRUE,NULL) ;
	g_object_set(G_OBJECT(dsrc), "max-bytes", 300 ,NULL) ;
	g_object_set(G_OBJECT(dsrc), "do-timestamp", TRUE ,NULL) ;
	g_object_set(G_OBJECT(dsrc), "min-percent", 50 ,NULL) ;
	g_signal_connect(G_OBJECT(dsrc), "need-data", G_CALLBACK(src_dfw), dfw) ;
	g_signal_connect(G_OBJECT(dsrc), "enough-data", G_CALLBACK(src_dfs), dfs) ;
	return TRUE ;
}

gboolean dcvConfigAppSink(GstAppSink *vsink,sink_sample_fn_t sink_ns, void *d_samp, sink_preroll_fn_t sink_pre, void *d_pre, sink_eos_fn_t eosRcvd, void *d_eos)  
{
	if (vsink == NULL) return FALSE ;
	g_signal_connect(GST_APP_SINK_CAST(vsink),"new-sample", sink_newsample,d_samp) ;
	g_signal_connect(GST_APP_SINK_CAST(vsink),"new-preroll", sink_newpreroll,d_pre) ;
	g_signal_connect(GST_APP_SINK_CAST(vsink),"eos", eosRcvd,d_eos) ;
	g_object_set(G_OBJECT(vsink), "emit-signals", TRUE,NULL) ;
	g_object_set(G_OBJECT(vsink), "drop", FALSE, NULL) ;
	g_object_set(G_OBJECT(vsink), "wait-on-eos", TRUE, NULL) ;
	return TRUE;
}
void dataFrameWrite(GstAppSrc *s, guint length, gpointer data)
{
	srcstate_e *pD = (srcstate_e *)data ;
	if (*pD != G_WAITING) {
		g_print("%s Needs data\n", GST_ELEMENT_NAME(GST_ELEMENT_CAST(s))) ;
		*pD = G_WAITING;
	}
}
void dataFrameStop(GstAppSrc *s, gpointer data)
{
	srcstate_e *pD = (srcstate_e *)data ;
	if (*pD != G_BLOCKED) {
	g_print("%s full\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(s))) ;
	*pD = G_BLOCKED;
	}
}
