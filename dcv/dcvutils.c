#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "rseq.h"
#include "gutils.hpp"
#include "gstdcv.h"
#include "dsopencv.hpp"


bufferCounter_t inbc,outbc;
extern gboolean terminate ;
int dcvGstDebug = 0 ;
int strictCheck = 0;

#define true 1


/** Buffer Manipulation **/
#include <sys/time.h>
void dcvTagBuffer(void *A, int isz, void *B, int osz)
{
	int i;
	static int count=0;
	struct timeval Tv;
	struct timezone tz;
	u32 *pA = (u32 *)A ;
	tag_t *pd = (tag_t *)B ;
	pd->count = count++ ;
	gettimeofday(&Tv,&tz) ;
	pd->tstmp = ((Tv.tv_sec & (0x0FFF)) << 20) | (Tv.tv_usec & 0x000FFFFF)  ;
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

gint dcvTimeDiff(struct timeval t1, struct timeval t2)
{
	gint t = 1000000*(t1.tv_sec - t2.tv_sec) ;
	t += t1.tv_usec - t2.tv_usec ;
	return t ;
}

gint dcvLengthOfStay(dcv_BufContainer_t *k)
{
	struct timeval tn;
	struct timezone tz;
	gettimeofday(&tn,&tz) ;
	return tn.tv_sec - k->ctime.tv_sec ;
}

void dcvBufContainerFree(dcv_BufContainer_t *P)
{
	if (!P) return ;
	gst_buffer_unref(P->nb) ;
	gst_caps_unref(P->caps) ;
	return ;
}

gboolean dcvMatchBuffer(void *A, int osz, void *B, int isz)
{
	gboolean retval = FALSE ;
	int i;
	u32 *pA = (u32 *)A ;
	tag_t *pd = (tag_t *)B ;
	u32 hsh=0;
	int sequence[1024];
	if (dcvGstDebug  & 0x03) g_print("dcvMatchB: Seq of size %d: count(%u) tstmp=%u hash=%u: ", pd->seqsize,pd->count,pd->tstmp,pd->checksum)  ;
	g_assert(pd->seqsize < 1024) ;
	g_assert(pd->seqsize == RSEQSIZE) ;
	for (i=0; i<pd->seqsize; i++) {
		sequence[i] = pd->seq[i] ;
		if (dcvGstDebug & 0x03) g_print("pA[%u]=%u ",sequence[i],pA[sequence[i]]) ;
		hsh ^= pA[sequence[i]] ;
	}
	if (dcvGstDebug & 0x03) g_print("Checksum = %u ..", hsh) ;
	hsh ^= pd->checksum ;
	if (hsh == 0) { retval = TRUE ; }
	else if (strictCheck) retval = FALSE ;
	else retval = TRUE ;
	if (dcvGstDebug & 0x03) g_print("hsh = %u (%s)\n", hsh, (hsh == 0? "match":"fail")) ;
	return retval ;
}

gint dcvMatchContainer (gconstpointer vq, gconstpointer tag )
{
	dcv_BufContainer_t *pA = (dcv_BufContainer_t *)vq ;
	tag_t *pB = (tag_t *)tag ;
	gboolean retval=FALSE ;
	GstMemory *vmem,*odmem;
	GstMapInfo vmap,odmap;

	vmem = gst_buffer_get_all_memory(pA->nb) ;
	if (gst_memory_map(vmem, &vmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in vbuffer\n") ; }
	retval = dcvMatchBuffer((void *)vmap.data,(int)vmap.size,(void *)pB,sizeof(tag_t)) ;
	gst_memory_unmap(vmem,&vmap) ;
	if (retval == TRUE) return 0 ;
	else return 1 ;
}

int dcvFindMatchingContainer(GQueue *q, dcv_BufContainer_t *d, tag_t *T)
{
	GList *p ;
	int rval=-1 ;
	if (q==NULL) goto GETOUT;
	else
	{
		GstMemory *tmem;
		GstMapInfo tmap;
		tmem = gst_buffer_get_all_memory(d->nb) ;
		if (gst_memory_map(tmem, &tmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in vbuffer\n") ; }
		memcpy((void *)T,tmap.data,sizeof(tag_t)) ;
		g_print("Recevied Tag:count=%u tstmp=%u \n ", T->count,T->tstmp) ;
		if (dcvGstDebug) g_print("Preparing to find matching container for count=%u tstmp=%u seqsize=%u\n",T->count,T->tstmp,T->seqsize) ;

		if ((p = g_queue_find_custom(q,(void *)T,dcvMatchContainer)) == NULL) 
		{
			rval = -1 ;
			g_print("No match found\n") ;
		}
		else
			rval =  g_queue_link_index(q,p) ;
		gst_memory_unmap(tmem, &tmap) ;
	}
GETOUT:
	return rval ;
}

int getTagSize()
{
	int sz = sizeof(tag_t) ;
	return sz ;
}

/** Sink and Source Handlers **/
void eosRcvd(GstAppSink *slf, gpointer D)
{
	if (dcvGstDebug) g_print("eosRcvd:Eos on %s\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(slf))) ;
	if (D) {
	gboolean *eos = (gboolean *)D ;
	*eos = TRUE ;
	}
}

void eosRcvdSrc(GstAppSrc *slf, gpointer D)
{
	if (dcvGstDebug) g_print("eosRcvdSrc:Eos on %s\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(slf))) ;
	gboolean *eos = (gboolean *)D ;
	*eos = TRUE ;
}


gboolean sink_pushbufferToQueue(GstBuffer *gb,gpointer pD)
{
	dcv_bufq_loc_t *D = (dcv_bufq_loc_t *)pD ;
	dcv_BufContainer_t *bcnt = malloc(sizeof(dcv_BufContainer_t)) ;
	//bcnt->nb = gst_buffer_copy_deep(gb) ;
	bcnt->nb = gb ;
	bcnt->caps = D->caps ;
	gst_buffer_ref(bcnt->nb) ;
	gettimeofday(&bcnt->ctime,&bcnt->ctz) ;
	g_queue_push_tail(D->pD->bufq,bcnt) ;
	gettimeofday(&D->pD->lastData,&D->pD->tz) ;
	D->pD->entries++ ;
	if (dcvGstDebug) 
		g_print("Added buffer number %d [size=%d] to queue [Total=%d]\n",
			g_queue_get_length(D->pD->bufq),gst_buffer_get_size(gb),D->pD->entries) ;
	return true;
}


int dcvBufQInit(dcv_bufq_t *P)
{
	P->bufq = g_queue_new() ;
	P->entries = 0;
}

GstBuffer * dcvProcessFn(GstBuffer *vbuf, GstCaps *gcaps, GstBuffer *dbuf,dcvFrameData_t *df, gpointer execFn, GstBuffer **newvb)
{
	gst_dcv_stage_t *F = (gst_dcv_stage_t *)execFn ;
	if (F != NULL)
		return dcvProcessStage(vbuf,gcaps,dbuf,df,F->sf,newvb) ;
	else 
		return dcvProcessStage(vbuf,gcaps,dbuf,df,NULL,newvb) ;
}
