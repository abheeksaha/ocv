#ifndef GBUS_HPP
#define GBUS_HPP

#define validstate(k) ((k>=0) && (k<=4))

typedef void (*src_dfw_fn_t)(GstAppSrc *, guint, gpointer) ;
typedef void (*src_dfs_fn_t)(GstAppSrc *, gpointer) ;
gboolean dcvConfigAppSrc(GstAppSrc *dsrc, src_dfw_fn_t src_dfw, void *dfw, src_dfs_fn_t src_dfs, void *dfs ) ;

typedef void (*sink_preroll_fn_t)(GstAppSink *, gpointer) ;
typedef void (*sink_sample_fn_t)(GstAppSink *, gpointer) ;
typedef void (*sink_eos_fn_t)(GstAppSink *, gpointer) ;
gboolean dcvConfigAppSink(GstAppSink *vsink,sink_sample_fn_t sink_ns, void *d_samp, sink_preroll_fn_t sink_pre, void *d_pre, sink_eos_fn_t eosRcvd, void *d_eos)  ;

void tagbuffer(void *A, int isz, void *B, int osz) ;
gboolean matchbuffer(void *A, int isz, void *B, int osz) ;

void eosRcvd(GstAppSink *slf, gpointer D) ;
gboolean listenToBus(GstElement *pipeline, GstState * cstate, GstState *ostate, unsigned int tms) ;
void testStats(GstElement *tst) ;
void sink_newpreroll(GstAppSink *slf, gpointer d) ;
void sink_newsample(GstAppSink *slf, gpointer d) ;

#endif
