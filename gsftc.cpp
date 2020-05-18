/**********************************************************************
 * Simple framing protocol over TCP for RTP packets.
 * Trying to solve the corruptionn problem
 *
 * *******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include "gsftc.hpp"

dcv_ftc_t * dcvFtConnInit(char *inaddress, unsigned short inport, char *outaddress, unsigned short outport)
{
	dcv_ftc_t *D = malloc(sizeof(dcv_ftc_t)) ;
	D->obuf  = calloc(MAXBYTESINARRAY,sizeof(char)) ;
	D->spaceleft = MAXBYTESINARRAY ;
	D->totalbytes = MAXBYTESINARRAY ;
	D->pbuf = D->obuf ;
	D->insock = D->outsock = D->servsock = -1 ;
	D->sequence = 231131;
	D->seqExpected = -1 ;
	D->eosIn = FALSE ;
	D->eosOut = FALSE ;
	if (inaddress != NULL) {
		g_print("Inaddress:%s inport:%d\n",inaddress,inport) ;
		if ( (D->servsock = socket(AF_INET,SOCK_STREAM,0)) < 0) {
			g_print("Socket creation failed!:%s\n",strerror(errno)) ;
			goto RETURNNULL ;
		}
		/** Create the server address **/
		memset((void *)&D->serv_addr, sizeof(sockaddr_in),0);
		D->serv_addr.sin_family = AF_INET;
		inet_aton(inaddress,&D->serv_addr.sin_addr) ;
		D->serv_addr.sin_port = htons(inport);
		if (bind(D->servsock, (struct sockaddr *) &D->serv_addr, sizeof(D->serv_addr)) < 0) {
			g_print("Bind error:%s\n",strerror(errno)) ;
			goto RETURNNULL ;
		}
	}
	if (outaddress != NULL) {
		g_print("Outaddress:%s outport:%d\n",outaddress,outport) ;
		if ( (D->outsock = socket(AF_INET,SOCK_STREAM,0)) < 0) {
			g_print("Outbound Socket creation failed!:%s\n",strerror(errno)) ;
			goto RETURNNULL ;
		}
		memset((void *)&D->dstaddr, sizeof(sockaddr_in),0);
		D->dstaddr.sin_family = AF_INET;
		inet_aton(outaddress,&D->dstaddr.sin_addr) ;
		D->dstaddr.sin_port = htons(outport);
	}

	return D ;
RETURNNULL:
	free(D->obuf) ;
	free(D) ;
	return NULL ;
}

gboolean dcvFtConnStart(dcv_ftc_t *D)
{
	g_print("Trying to start connections!\n") ;
	if (D->outsock != -1) {
		if (connect(D->outsock,(struct sockaddr *) &D->dstaddr,sizeof(struct sockaddr)) < 0) {
			g_print("ERROR connecting:%s\n",strerror(errno));
			return FALSE ;
		}
	}
	else {
		g_print("Connected to server:%s:%u\n",inet_ntoa(D->dstaddr.sin_addr),D->dstaddr.sin_port) ;
		return TRUE ;
	}
	if (D->servsock != -1) {
		g_print("Opening server connection\n") ;
		if (listen(D->servsock,5) == -1) {
			g_print("Listen error:%s\n",strerror(errno)) ;
			return FALSE ;
		}
		unsigned int clilen = sizeof(D->cli_addr) ;
		if ((D->insock = accept(D->servsock,(sockaddr *)&D->cli_addr,&clilen)) < 0)
		{
			g_print("Connection accept failure:%s\n",strerror(errno)) ;
			return FALSE ;
		}
		else {
			g_print("Received connection request from ") ;
			g_print("%s:%u\n",inet_ntoa(D->cli_addr.sin_addr), ntohs(D->cli_addr.sin_port)) ;
			return TRUE ;
		}
	}
}


#define NETBUFSIZE 8192
#define max(a,b) ((a) > (b) ? (a):(b))
void dcvAppSrcFrameWrite(GstAppSrc *slf, guint length, gpointer d)
{
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	int nbytes ;
	gboolean forceflush=FALSE ;
	while ( (nbytes = recv(D->insock,D->pbuf,NETBUFSIZE,0)) >= 0) {
		/** See how much space there is in the array **/
		if (nbytes == 0) {
			g_print("Connection closed!\n") ;
			forceflush = TRUE ;
		}
		D->pbuf += nbytes;
		D->spaceleft -= nbytes ;
		g_print("Received %d bytes, spaceleft=%d\n",nbytes,D->spaceleft) ;
		while ((D->spaceleft < NETBUFSIZE) || (forceflush && (D->spaceleft < D->totalbytes)) )
		{
		/** Clear out the buffer, make space **/
			unsigned int *pfh = (unsigned int *)D->obuf ;
			int bsize;
			struct timeval tv ;
			g_assert(*pfh == uw) ;
			pfh++ ;
			if (D->seqExpected != -1) { 
				g_assert(*pfh == D->seqExpected) ;
				D->seqExpected++ ;
			}
			else 
				D->seqExpected = *pfh ;
			pfh++ ;
			tv.tv_usec = *pfh & 0xffff ;
			tv.tv_sec = *pfh >> 16 ;
			pfh++ ;
			bsize = *pfh ;
			g_assert(bsize < D->totalbytes) ;
			GstBuffer * gb = gst_buffer_new_allocate(NULL,bsize,NULL) ;
			if (gb == NULL ) {
				g_print("Ran out of buffers in the pool!\n") ; 
				g_assert(gb) ;
			}
			GstMemory *bmem;
			GstMapInfo bmap;
			bmem = gst_buffer_get_all_memory(gb) ;
			if (gst_memory_map(bmem, &bmap, GST_MAP_WRITE) != TRUE) { g_printerr("Couldn't map memory in send buffer for reading\n") ; }
			memcpy(bmap.data,*pfh,bsize) ;
			GST_BUFFER_PTS(gb);
			GstFlowReturn ret = gst_app_src_push_buffer(slf,gb) ;
			if (ret == GST_FLOW_ERROR) {
				g_print("Couldn't push buffer to app src\n") ; 
				g_assert(ret != GST_FLOW_ERROR) ;
			}
			memmove(D->obuf,&D->obuf[SIZEOFFRAMEHDR+bsize],SIZEOFFRAMEHDR+bsize) ;
			D->spaceleft += SIZEOFFRAMEHDR+bsize;
			D->pbuf = &D->obuf[D->totalbytes - D->spaceleft] ;
		}
	}
}

void dcvAppSrcFrameStop(GstAppSrc *slf, gpointer d )
{
	return ;
}

GstFlowReturn dcvAppSinkNewPreroll(GstAppSink *slf, gpointer d)
{
	GstCaps * dbt = NULL ;
	gchar *gs;
	if (d == NULL) 
	{
		g_print("New Preroll in %s:\n",GST_ELEMENT_NAME(slf)) ;
		return GST_FLOW_OK ;
	}
	GstSample *gsm ;
	if ((gsm = gst_app_sink_pull_preroll(slf)) != NULL)
	{
		g_print("New Preroll in %s: --",GST_ELEMENT_NAME(slf)) ;
		dbt = gst_sample_get_caps(gsm) ;
		gs = gst_caps_to_string(dbt);
		g_print(" caps %s \n",gs) ;
		g_free(gs) ;
		gst_sample_unref(gsm) ;
		gst_caps_unref(dbt) ;
		return GST_FLOW_OK;
	}
	else
		return GST_FLOW_ERROR;
}

GstFlowReturn dcvAppSinkNewSample(GstAppSink *slf, gpointer d)
{
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	if (d == NULL) {
		g_print("New Sample in %s:\n",GST_ELEMENT_NAME(slf)) ;
		return GST_FLOW_OK;
	}

	GstSample *gsm ;
	if ((gsm = gst_app_sink_pull_sample(slf)) != NULL) {
		GstBufferList *glb = gst_sample_get_buffer_list(gsm) ;
		if (glb != NULL) {
			if (gst_buffer_list_foreach(glb,dcvSendBuffer,D) == TRUE)
				return GST_FLOW_OK;
			else 
				return GST_FLOW_ERROR;
		}
		else 
		{
			GstBuffer *gb = gst_sample_get_buffer(gsm) ;
			if (dcvSendBuffer(gb,(gpointer)D) == TRUE)
				return GST_FLOW_OK;
			else
				return GST_FLOW_ERROR ;
		}
	}
	else
		return GST_FLOW_ERROR ;
}

gboolean dcvSendBuffer (GstBuffer *b, gpointer d)
{
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	static char framehead[SIZEOFFRAMEHDR] ;
	unsigned int *pfh = (unsigned int *)framehead ;
	struct timeval tv;
	struct timezone tz;
	unsigned int ptm;
	GstMemory *bmem;
	GstMapInfo bmap;
	bmem = gst_buffer_get_all_memory(b) ;
	if (gst_memory_map(bmem, &bmap, GST_MAP_READ) != TRUE) { g_printerr("Couldn't map memory in send buffer\n") ; }
	gboolean rval = TRUE ;
	*pfh++ = uw ; /** First 32 bits **/
	*pfh++ = D->sequence++ ; /** Next 32 bits **/
	gettimeofday(&tv,&tz) ;
	*pfh = tv.tv_usec & 0xffff ;
	*pfh |= (tv.tv_sec & 0xffff) << 16 ;
	*pfh++ ;
	*pfh = (unsigned int)bmap.size;
	if ( send(D->outsock,framehead,SIZEOFFRAMEHDR, MSG_MORE) == -1) {
		return FALSE ;	
	}

	if ( (send(D->outsock, (void *)bmap.data,(int)bmap.size,0)) == -1) { 
		g_printerr("Couldn;t map buffer for sending\n") ;
		rval = FALSE ;
	}
	gst_memory_unmap(bmem,&bmap) ;
	return rval ;
}





