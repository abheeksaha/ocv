#include <stdio.h>
#include <ctype.h>

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

void walkPipeline(GstBin *bin)
{
	GstIterator * git;
	GValue gv = {0,};
	GstIteratorResult gres;
	GstElement * ele;
	git = gst_bin_iterate_sorted(bin) ;
	while ( (gres = gst_iterator_next(git,&gv)) != GST_ITERATOR_ERROR) {
		if (gres == GST_ITERATOR_DONE) { g_print("]\n") ; break ; }
		else if (gres == GST_ITERATOR_RESYNC) { gst_iterator_resync(git) ; continue ; }
		GstElement *ele= GST_ELEMENT_CAST(g_value_get_object(&gv)) ;
		g_print("Elem:%s Has clock:%s ",GST_ELEMENT_NAME(ele),ele->clock == NULL? "no":"yes") ;
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


void tagbuffer(void *A, int isz, void *B, int osz)
{
	int i;
	unsigned long *pA = (unsigned long *)A ;
	unsigned long *pd = (unsigned long *)B ;
	/** First write the random sequence into the output **/
	*pd++ = RSEQSIZE ;
	for (i=0; i<RSEQSIZE; i++) 
		*pd++ = rseq[i] ;
	*pd=0;
	/* Now write the checksum at the end */
	for (i=0; i<RSEQSIZE; i++)
	{
		if (rseq[i] >= isz/sizeof(unsigned long)) {
			g_print("%lu is too big for %lu \n",
				rseq[i], isz) ;
			g_assert(rseq[i] < (isz/sizeof(unsigned long))) ;
		}
		*pd ^= pA[rseq[i]] ;
	}
	g_print("Final hash for sequence size %u is %lu\n",RSEQSIZE, *pd) ;
}


gboolean matchbuffer(void *A, int isz, void *B, int osz)
{
	gboolean retval = FALSE ;
	int i;
	unsigned long *pA = (unsigned long *)A ;
	unsigned long *pd = (unsigned long *)B ;
	unsigned long hsh=0;
	int seqsize = *pd++ ;
	static int sequence[1024];
	g_assert(seqsize < 1024) ;
	g_print("Seq of size %d: ", seqsize)  ;
	for (i=0; i<seqsize; i++) {
		sequence[i] = *pd++ ;
		g_print("%lu ",sequence[i]) ;
	}
	hsh = *pd ;
	g_print("Checksum = %lu ..", *pd) ;
	for (i=0; i<seqsize; i++) {
		hsh ^= pA[sequence[i]] ;
	}
	if (hsh == 0) { retval = TRUE ; }
	else retval = FALSE ;
	g_print("hsh = %lu (%s)\n", hsh, (hsh == 0? "match":"fail")) ;
	return retval ;
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
void sink_newpreroll(GstAppSink *slf, gpointer d)
{
	unsigned int *D = (unsigned int *)d ;
	*D += 1 ;
	g_print("New Preroll in %s: %d\n",GST_ELEMENT_NAME(slf),*D) ;
}

void sink_newsample(GstAppSink *slf, gpointer d)
{
	unsigned int *D = (unsigned int *)d ;
	*D += 1 ;
	g_print("New Sample in %s: %d\n",GST_ELEMENT_NAME(slf),*D) ;
}

gboolean dcvConfigAppSrc(GstAppSrc *dsrc, src_dfw_fn_t src_dfw, void *dfw, src_dfs_fn_t src_dfs, void *dfs )
{
	g_object_set(G_OBJECT(dsrc), "format", GST_FORMAT_TIME,NULL) ;
	g_object_set(G_OBJECT(dsrc), "stream-type", GST_APP_STREAM_TYPE_STREAM,NULL) ;
	g_object_set(G_OBJECT(dsrc), "emit-signals", TRUE,NULL) ;
	g_object_set(G_OBJECT(dsrc), "block", TRUE,NULL) ;
	g_object_set(G_OBJECT(dsrc), "max-bytes", 300 ,NULL) ;
	g_object_set(G_OBJECT(dsrc), "do-timestamp", TRUE,NULL) ;
	g_object_set(G_OBJECT(dsrc), "min-percent", 50 ,NULL) ;
	g_signal_connect(G_OBJECT(dsrc), "need-data", G_CALLBACK(src_dfw), dfw) ;
	g_signal_connect(G_OBJECT(dsrc), "enough-data", G_CALLBACK(src_dfs), dfs) ;
	return TRUE ;
}

gboolean dcvConfigAppSink(GstAppSink *vsink,sink_sample_fn_t sink_ns, void *d_samp, sink_preroll_fn_t sink_pre, void *d_pre, sink_eos_fn_t eosRcvd, void *d_eos)  
{
	g_signal_connect(GST_APP_SINK_CAST(vsink),"new-sample", sink_newsample,d_samp) ;
	g_signal_connect(GST_APP_SINK_CAST(vsink),"new-preroll", sink_newpreroll,d_pre) ;
	g_signal_connect(GST_APP_SINK_CAST(vsink),"eos", eosRcvd,d_eos) ;
	g_object_set(G_OBJECT(vsink), "emit-signals", TRUE,NULL) ;
	g_object_set(G_OBJECT(vsink), "drop", FALSE, NULL) ;
	g_object_set(G_OBJECT(vsink), "wait-on-eos", TRUE, NULL) ;
	return TRUE;
}
