/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Abheek Saha <<user@hostname.org>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-dcv
 *
 * FIXME:Describe dcv here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! dcv ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/time.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

#include "../rseq.h"
#include "gutils.hpp"
#include "gstdcv.h"
#include "dsopencv.hpp"

GST_DEBUG_CATEGORY_STATIC (gst_dcv_debug);
#define GST_CAT_DEFAULT gst_dcv_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_DCV_EXEC_FN,
  PROP_DCV_MODE
};
#define MAX_STAY 1

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate video_sink_factory = GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,format=BGR")
    );

static GstStaticPadTemplate rtp_sink_factory = GST_STATIC_PAD_TEMPLATE ("rtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );

static GstStaticPadTemplate video_src_factory = GST_STATIC_PAD_TEMPLATE ("video_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,format=BGR")
    );

static GstStaticPadTemplate rtp_src_factory = GST_STATIC_PAD_TEMPLATE ("rtp_src",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_dcv_parent_class parent_class
G_DEFINE_TYPE (Gstdcv, gst_dcv, GST_TYPE_ELEMENT);

static void gst_dcv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dcv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_dcv_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_dcv_chain_video (GstPad * pad, GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_dcv_chain_gst (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the dcv's class */
static void
gst_dcv_class_init (GstdcvClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dcv_set_property;
  gobject_class->get_property = gst_dcv_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DCV_EXEC_FN,
      g_param_spec_pointer ("stage-function", "stagef", "How to process the function",
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DCV_MODE,
      g_param_spec_int ("grcvrMode", "grcvrMode", "Intermediate/Terminal",
          0,GRCVR_MODEMAX, GRCVR_LAST, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "dcv",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "Abheek Saha <<abheek.saha@gmail.com>>");

  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&video_src_factory));
  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&rtp_src_factory));
  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&rtp_sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static gboolean gstQueryFunc(GstPad *pad, GstObject *parent, GstQuery *query)
{
  	Gstdcv *gdcv = GST_DCV (parent);
	g_print ("%s: Received query of type %u\n", 
		gst_element_get_name(GST_ELEMENT(gdcv)),GST_QUERY_TYPE(query)) ;
	gboolean ret = TRUE ;
	switch (GST_QUERY_TYPE(query)) {
		case GST_QUERY_CAPS:
		{
			GstCaps *filter, *caps;

			gst_query_parse_caps (query, &filter);
			g_print("%s: Caps query received:%s\n",
					gst_element_get_name(GST_ELEMENT(gdcv)),
					gst_caps_to_string(filter)) ;
			if (pad == gdcv->rtp_in || pad == gdcv->rtp_out)
			{
				if (gdcv->dcaps == NULL)
				caps = gst_caps_new_simple ("application/x-rtp", 
					"media", G_TYPE_STRING, "application", 
					"encoding-name", G_TYPE_STRING, "X-GST", 
					NULL);
				else caps = gdcv->dcaps ;
			}
			else 
			{
				if (gdcv->vcaps == NULL) 
				caps = gst_caps_new_simple ("video/x-raw", 
					"format", G_TYPE_STRING, "BGR", 
					NULL);
				else caps = gdcv->vcaps ;
			}
			g_print("dcvTerminal: Returning query response %s\n",gst_caps_to_string(caps)) ;
			gst_query_set_caps_result (query, caps);
			gst_caps_unref (caps);
			ret = TRUE;
			break;
    		}
		case GST_QUERY_ACCEPT_CAPS: 
		{
			GstCaps *caps ;
			gst_query_parse_accept_caps(query,&caps) ;
			g_print("dcvTerminal: Accept caps:%s\n",gst_caps_to_string(caps)) ;
			ret = TRUE;
          		gst_query_set_accept_caps_result (query, TRUE);
			break ;
		}
		default:
			g_print("Couldn't understand this query\n") ;
			ret = gst_pad_query_default(pad,parent,query) ;
			break;
	}
	return ret ;
}
static void
gst_dcv_init (Gstdcv * filter)
{
  filter->video_in = gst_pad_new_from_static_template (&video_sink_factory, "video_sink");
  gst_pad_set_event_function (filter->video_in, GST_DEBUG_FUNCPTR(gst_dcv_sink_event));
  gst_pad_set_chain_function (filter->video_in, GST_DEBUG_FUNCPTR(gst_dcv_chain_video));
  gst_pad_set_query_function_full (filter->video_in, gstQueryFunc,NULL,NULL) ;
// GST_PAD_SET_PROXY_CAPS (filter->video_in);
  gst_element_add_pad (GST_ELEMENT (filter), filter->video_in);

  filter->rtp_in = gst_pad_new_from_static_template (&rtp_sink_factory, "rtp_sink");
  gst_pad_set_event_function (filter->rtp_in, GST_DEBUG_FUNCPTR(gst_dcv_sink_event));
  gst_pad_set_chain_function (filter->rtp_in, GST_DEBUG_FUNCPTR(gst_dcv_chain_gst));
  gst_pad_set_query_function_full (filter->rtp_in, gstQueryFunc,NULL,NULL) ;
//  GST_PAD_SET_PROXY_CAPS (filter->rtp_in);
  gst_element_add_pad (GST_ELEMENT (filter), filter->rtp_in);

  filter->video_out = gst_pad_new_from_static_template (&video_src_factory, "video_src");
//  GST_PAD_SET_PROXY_CAPS (filter->video_out);
  gst_pad_set_query_function_full (filter->video_out, gstQueryFunc,NULL,NULL) ;
  gst_element_add_pad (GST_ELEMENT (filter), filter->video_out);

  filter->rtp_out = gst_pad_new_from_static_template (&rtp_src_factory, "rtp_src");
//  GST_PAD_SET_PROXY_CAPS (filter->rtp_out);
  gst_pad_set_query_function_full (filter->rtp_out, gstQueryFunc,NULL,NULL) ;
  gst_element_add_pad (GST_ELEMENT (filter), filter->rtp_out);

  filter->silent = FALSE;
  filter->execFn = NULL ;
  filter->grcvrMode = GRCVR_LAST ;
  filter->vcaps = filter->dcaps = NULL ;
  filter->dcvDataRx = filter->dcvVideoRx = 0;
  dcvBufQInit(&filter->Q.dataqueue) ;
  dcvBufQInit(&filter->Q.olddataqueue) ;
  dcvBufQInit(&filter->Q.videoframequeue) ;
}

static int dcvProcessQueuesDcv(Gstdcv * filter)
{
	int stay=0;
	dcv_BufContainer_t *dataFrameContainer;
	struct timeval nowTime ;
	struct timezone nz;
	static struct timeval lastCheck ;
	static struct timezone tz;
	grcvr_mode_e grcvrMode = (grcvr_mode_e)filter->grcvrMode ;
	dcv_data_struct_t *pd = &(filter->Q);
	dcvFrameData_t *Dv = &(filter->Dv) ;
	gpointer videoFrameWaiting = NULL ;
	gpointer dataFrameWaiting = NULL ;
	tag_t T ;
	GstFlowReturn ret = GST_FLOW_ERROR ;

	if (filter->vfmatch == -1) {
		/** We failed last time, see if something has changed **/
		if ((dcvTimeDiff(pd->videoframequeue.lastData,lastCheck) <= 0) &&
		    (dcvTimeDiff(pd->olddataqueue.lastData,lastCheck) <= 0) )
						return 0 ;
	}
	else if (lastCheck.tv_sec == 0)
		gettimeofday(&lastCheck,&tz) ;

	if (grcvrMode == GRCVR_FIRST) {
			dataFrameContainer = NULL ; 
	}
	else if ( (dataFrameContainer = (dcv_BufContainer_t *)g_queue_pop_head(pd->olddataqueue.bufq)) == NULL) {
		
		       return -1 ;
	}

	else if ( ((filter->vfmatch = dcvFindMatchingContainer(pd->videoframequeue.bufq,dataFrameContainer,&T)) == -1) ) {
		GST_LOG_OBJECT(GST_OBJECT(filter),"no match found: vfmatch=%d (vq=%u dq=%u)\n",filter->vfmatch, 
				g_queue_get_length(pd->videoframequeue.bufq), 
				g_queue_get_length(pd->olddataqueue.bufq)) ;
#if 1
		if ( (stay = dcvLengthOfStay(dataFrameContainer)) > MAX_STAY)  {
				dcvBufContainerFree(dataFrameContainer) ;
				free(dataFrameContainer) ;
				g_print("Dropping data buffer, no match for too long\n") ;
		}
		else 
			g_queue_push_head(pd->olddataqueue.bufq,dataFrameContainer) ;
#endif
		gettimeofday(&lastCheck,&tz) ;
		GST_LOG_OBJECT(GST_OBJECT(filter),"Recording last failed check at %u:%u\n",lastCheck.tv_sec, lastCheck.tv_usec) ;
		return 0 ;
	}
GRCVR_PROCESS:
	{
		dcv_fn_t stagef = (dcv_fn_t)filter->execFn;
		dcv_BufContainer_t *qe = ((dcv_BufContainer_t *)g_queue_pop_nth(pd->videoframequeue.bufq,filter->vfmatch)) ;
		GstBuffer * newVideoFrame = NULL ;
		GstBuffer * newDataFrame = NULL ;
		if (dataFrameContainer != NULL) {
			dataFrameWaiting = dataFrameContainer->nb;
			videoFrameWaiting = qe->nb ;
			GstCaps *vcaps = qe->caps ;
#if 1
			if (stagef != NULL)
				newDataFrame = dcvProcessFn( videoFrameWaiting, vcaps,dataFrameWaiting, Dv, filter->execFn, &newVideoFrame ) ;
			else 
#endif
			{
				GST_LOG_OBJECT(GST_OBJECT(filter),"Bypassing video frame processing\n") ;
				newDataFrame = dcvProcessFn( videoFrameWaiting, vcaps,dataFrameWaiting, Dv, NULL, &newVideoFrame ) ;
			}
			if (gst_pad_is_linked(filter->video_out))
			{
				GST_LOG_OBJECT(GST_OBJECT(filter),"Transmitting video frame\n") ;
				gst_pad_push(filter->video_out,newVideoFrame) ;
			}
			Dv->num_frames++ ;
			GST_LOG_OBJECT(GST_OBJECT(filter),
			"State of queues:vq=%d dq=%d\n", g_queue_get_length(pd->videoframequeue.bufq), 
					g_queue_get_length(pd->olddataqueue.bufq)) ;
					
				

			if (grcvrMode == GRCVR_INTERMEDIATE) 
			{
				newDataFrame = gst_buffer_copy(dataFrameWaiting) ;
				if (gst_pad_is_linked(filter->rtp_out)) {
					ret = gst_pad_push(filter->rtp_out,newDataFrame) ;
				}
				GST_LOG_OBJECT(GST_OBJECT(filter),
					"Pushing data frame .. retval=%d buffers=%d vq=%d dq=%d\n",
						ret,++(filter->vbufsnt), g_queue_get_length(pd->videoframequeue.bufq), g_queue_get_length(pd->olddataqueue.bufq)) ;
			}
			/** Record the time **/
			{
				struct timeval prcTime ;
				struct timezone ntz;
				unsigned long tx_sec, tx_usec ;
				double tg_usec ;
				gettimeofday(&prcTime,&ntz) ;
				tx_usec = T.tstmp & 0xfffff ;
				tx_sec = (T.tstmp >> 20) & 0xfff ;
				prcTime.tv_sec &= 0xfff ;
				prcTime.tv_usec &= 0xfffff ;
				tg_usec = (double)((prcTime.tv_sec - tx_sec)%4096)*1000.0 + (double)(prcTime.tv_usec - tx_usec)/1000.0 ;
				if (tg_usec < 0) {
					g_print("Something wrong in time processing (%ld %ld proc) (%ld %ld current)\n",
							tx_sec, tx_usec, prcTime.tv_sec,prcTime.tv_usec) ;
				}
				else 
				{
					Dv->avgProcessTime += tg_usec ;
					g_print("Time processing (%ld %ld orig) (%ld %ld current)\n", tx_sec, tx_usec, prcTime.tv_sec,prcTime.tv_usec) ;
					g_print("Processing time for frame %d: %.4g msec: avg=%.4g\n",
							Dv->num_frames,tg_usec,Dv->avgProcessTime/Dv->num_frames) ;
				}
			}

			gst_buffer_unref(GST_BUFFER_CAST(videoFrameWaiting));
			gst_caps_unref(vcaps);
			gst_buffer_unref(GST_BUFFER_CAST(dataFrameWaiting));
		}
		/** Clean up the video frame queue **/
		g_print("\n") ;
		return 1 ;
	}
}

static void
gst_dcv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstdcv *filter = GST_DCV (object);
  GST_WARNING_OBJECT(object,"Setting property %u\n",gst_element_get_name(GST_ELEMENT(object)), prop_id) ;

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_DCV_EXEC_FN:
      filter->execFn = g_value_get_pointer(value) ;
      break ;
    case PROP_DCV_MODE:
      filter->grcvrMode = g_value_get_int(value) ;
      break ;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dcv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstdcv *filter = GST_DCV (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_DCV_EXEC_FN :
      g_value_set_pointer(value, filter->execFn) ;
      break ; 
    case PROP_DCV_MODE:
      g_value_set_int(value,filter->grcvrMode) ;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_dcv_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstdcv *filter;
  gboolean ret;

  filter = GST_DCV (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps * caps;

      gst_event_parse_caps (event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_dcv_chain_video (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  Gstdcv *filter = GST_DCV(parent);
  dcv_bufq_loc_t ld ;
  ld.pD = &(filter->Q.videoframequeue) ;
  ld.caps = gst_pad_get_current_caps(pad) ;

  if (filter->silent == FALSE)
  {
    g_print ("DCV Video frame rx: (%d) ", filter->dcvVideoRx) ;
    if (filter->dcvVideoRx == 0)
	g_print ("caps %s\n",gst_caps_to_string(ld.caps));
    else
	g_print ("\n") ;
  }
  filter->dcvVideoRx++ ;
  filter->vcaps = ld.caps ;

  if (sink_pushbufferToQueue(buf,&ld) ) {
	  dcvProcessQueuesDcv(filter) ;
  	return GST_FLOW_OK ;
  } else {
	  return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_dcv_chain_gst (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  Gstdcv *filter = GST_DCV(parent);
  dcv_bufq_loc_t ld ;
  ld.pD = &filter->Q.olddataqueue ;
  ld.caps = gst_pad_get_current_caps(pad) ;

  if (filter->silent == FALSE)
  {
    g_print ("DCV Data frame rx: (%d) (mode=%d) ", filter->dcvDataRx, filter->grcvrMode) ;
    if (filter->dcvDataRx == 0) g_print("caps %s\n",gst_caps_to_string(ld.caps));
    else g_print("\n") ;
  }
  filter->dcaps = ld.caps ;
  filter->dcvDataRx++ ;

  if (sink_pushbufferToQueue(buf,&ld) ) {
	  dcvProcessQueuesDcv(filter) ;
  	return GST_FLOW_OK ;
  } else {
	  return GST_FLOW_ERROR;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
dcv_init (GstPlugin * dcv)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template dcv' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_dcv_debug, "gst_dcv",
      0, "Template dcv");

  return gst_element_register (dcv, "dcv", GST_RANK_NONE,
      GST_TYPE_DCV);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstdcv"
#endif

/* gstreamer looks for this structure to register dcvs
 *
 * exchange the string 'Template dcv' with your dcv description
 */
#define PACKAGE_VERSION "1.0"
#define GST_LICENSE "GPL"
#define GST_PACKAGE_NAME "dcv"
#define GST_PACKAGE_ORIGIN "http://www.hsc.com"

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dcv,
    "dcv for dscope",
    dcv_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
