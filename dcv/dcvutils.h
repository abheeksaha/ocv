#ifndef GBUS_HPP
#define GBUS_HPP

#include <sys/time.h>
#define validstate(k) ((k>=0) && (k<=4))

#include "gstdcv.h"
typedef enum {
	EOS_VSINK,
	EOS_DSINK,
	EOS_USINK,
	EOS_VDISP,
	EOS_USRC,
	EOS_DSRC,
	MAX_EOS_TYPES
} eos_e ;
GST_DEBUG_CATEGORY_STATIC (my_category);
#define GST_CAT_DEFAULT my_category


typedef enum {
	G_WAITING,
	G_BLOCKED
} srcstate_e ;

typedef struct {
	srcstate_e state ;
	int length ;
	gboolean finished ;
}srcstate_t ;

int dcvBufQInit(dcv_bufq_t *P) ;


typedef void (*src_dfw_fn_t)(GstAppSrc *, guint, gpointer) ;
typedef void (*src_dfs_fn_t)(GstAppSrc *, gpointer) ;

typedef GstFlowReturn (*sink_preroll_fn_t)(GstAppSink *, gpointer) ;
typedef GstFlowReturn (*sink_sample_fn_t)(GstAppSink *, gpointer) ;
typedef void (*sink_eos_fn_t)(GstAppSink *, gpointer) ;
typedef void (*src_eos_fn_t)(GstAppSrc *, gpointer) ;
gboolean dcvConfigAppSink(GstAppSink *vsink,sink_sample_fn_t sink_ns, void *d_samp, sink_preroll_fn_t sink_pre, void *d_pre, sink_eos_fn_t eosRcvd, void *d_eos)  ;
gboolean dcvConfigAppSrc(GstAppSrc *dsrc, src_dfw_fn_t src_dfw, void *dfw, src_dfs_fn_t src_dfs, void *dfs, src_eos_fn_t eosRcvd, void *d_eos , GstCaps *caps) ;
void dcvAppSrcStatus(GstAppSrc *s, srcstate_t *st) ;
gboolean sink_pushbufferToQueue(GstBuffer *gb,gpointer pD) ;

/** Buffer Functions **/
void dcvTagBuffer(void *A, int isz, void *B, int osz) ;
gboolean dcvMatchBuffer(void *A, int isz, void *B, int osz) ;
gint dcvMatchContainer (gconstpointer A, gconstpointer B ) ;
int dcvFindMatchingContainer(GQueue *q, dcv_BufContainer_t *d, tag_t *T) ;
void dcvBufContainerFree(dcv_BufContainer_t *) ;
gint dcvLengthOfStay(dcv_BufContainer_t *k) ;
gint dcvTimeDiff(struct timeval t1, struct timeval t2) ;

void eosRcvd(GstAppSink *slf, gpointer D) ;
void eosRcvdSrc(GstAppSrc *slf, gpointer D) ;
gboolean listenToBus(GstElement *pipeline, GstState * cstate, GstState *ostate, unsigned int tms) ;
void testStats(GstElement *tst) ;
GstFlowReturn sink_newpreroll(GstAppSink *slf, gpointer d) ;
GstFlowReturn sink_newsample(GstAppSink *slf, gpointer d) ;
GstFlowReturn sink_trypullsample(GstAppSink *slf, dcv_bufq_t *d) ;
void walkPipeline(GstElement *p, guint level) ;
void dataFrameWrite(GstAppSrc *s, guint length, gpointer data) ;
void dataFrameStop(GstAppSrc *s,  gpointer data) ;


typedef struct {
	guint buffercount;
	guint bytecount;
} bufferCounter_t ;
/** Probe functions **/
GstPadProbeReturn cb_have_data (GstPad  *pad, GstPadProbeInfo *info, gpointer user_data) ;
GstPadProbeReturn cb_have_data_bl (GstPad  *pad, GstPadProbeInfo *info, gpointer user_data) ;
void dcvAttachBufferCounterIn(GstElement *e, bufferCounter_t *oc) ;
void dcvAttachBufferCounterOut(GstElement *e, bufferCounter_t *oc) ;

int getTagSize(void) ;


void bufferCounterInit(bufferCounter_t *i, bufferCounter_t *o) ;
void bufferCounterDump(int signalnum) ;

int dcvLocalDisplay(GstBuffer *gb, GstCaps *vcaps, GstAppSrc *vdisp, int num_frames) ;
GstBuffer * dcvProcessFn(GstBuffer *vbuf, GstCaps *gcaps, GstBuffer *dbuf,dcvFrameData_t *df, gpointer execFn, GstBuffer **newvb) ;
#endif
