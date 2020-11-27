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
#include "gstdcvrtpmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_dcvrtpmux_debug);
#define GST_CAT_DEFAULT gst_dcvrtpmux_debug

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
  PROP_DCV_MODE
};
#define MAX_STAY 1

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

#define gst_dcvrtpmux_parent_class parent_class
G_DEFINE_TYPE (Gstdcvrtpmux, gst_dcvrtpmux, GST_TYPE_ELEMENT);

static void gst_dcvrtpmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dcvrtpmux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

dcvrtpmux_bufq_loc_t * getQueue(GstObject *parent, GstPad *pad) ;
static gboolean gst_dcvrtpmux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_dcvrtpmux_chain_rtpbuffer (GstPad * pad, GstObject * parent, GstBuffer * buf);
static GstPad *gst_dcvrtpmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_dcvrtpmux_setup_sinkpad(GstElement * filter, GstPad *sinkpad) ;

/* GObject vmethod implementations */

/* initialize the dcvrtpmux's class */
static void
gst_dcvrtpmux_class_init (GstdcvrtpmuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dcvrtpmux_set_property;
  gobject_class->get_property = gst_dcvrtpmux_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "dcvrtpmux",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "Abheek Saha <<abheek.saha@gmail.com>>");

  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&sink_factory));
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_dcvrtpmux_request_new_pad);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static gboolean gstQueryFunc(GstPad *pad, GstObject *parent, GstQuery *query)
{
  	Gstdcvrtpmux *gdcvrtpmux = GST_DCVRTPMUX (parent);
	GST_LOG_OBJECT (parent,"dcvrtpmuxTerminal: Received query of type %u\n", GST_QUERY_TYPE(query)) ;
	gboolean ret = TRUE ;
	switch (GST_QUERY_TYPE(query)) {
		case GST_QUERY_CAPS:
		{
			GstCaps *filter, *caps;

			gst_query_parse_caps (query, &filter);
			GST_LOG_OBJECT(parent,"dcvrtpmuxTerminal: Caps query received:%s\n",gst_caps_to_string(filter)) ;
			caps = gst_caps_new_simple("application/x-rtp",NULL) ;
			GST_LOG_OBJECT(parent,"dcvrtpmuxTerminal: Returning query response %s\n",gst_caps_to_string(caps)) ;
			gst_query_set_caps_result (query, caps);
			gst_caps_unref (caps);
			ret = TRUE;
			break;
    		}
		case GST_QUERY_ACCEPT_CAPS: 
		{
			GstCaps *caps ;
			gst_query_parse_accept_caps(query,&caps) ;
			GST_LOG_OBJECT(parent,"dcvrtpmuxTerminal: Accept caps:%s\n",gst_caps_to_string(caps)) ;
			ret = TRUE;
          		gst_query_set_accept_caps_result (query, TRUE);
			break ;
		}
		default:
			GST_LOG_OBJECT(parent,"Couldn't understand this query\n") ;
			ret = gst_pad_query_default(pad,parent,query) ;
			break;
	}
	return ret ;
}

static GstPad *
gst_dcvrtpmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstDcvRtpMux_t *dcv_rtp_mux;
  GstPad *newpad;

  GST_LOG_OBJECT(element,"Request received for pad %s, caps:%s\n",req_name,gst_caps_to_string(caps)) ;
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_DCVRTPMUX (element), NULL);

  dcv_rtp_mux = GST_DCVRTPMUX (element);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING_OBJECT (dcv_rtp_mux, "request pad that is not a SINK pad");
    return NULL;
  }
  else if (req_name == NULL)
 {
	  GST_LOG_OBJECT(dcv_rtp_mux, "request pad name is null:\n") ;
	  return NULL ;
 }
  else if (strcmp(req_name,"sink_0") && strcmp(req_name, "sink_1")) {
	  GST_LOG_OBJECT(dcv_rtp_mux, "request pad name is wierd:%s\n",req_name) ;
	  return NULL ;
  }

  newpad = gst_pad_new_from_template (templ, req_name);
  if (newpad)
  {
	dcvrtpmux_bufq_loc_t *ppad = &(dcv_rtp_mux->padq[dcv_rtp_mux->nsinks]) ;
	dcv_rtp_mux->sinkpads[dcv_rtp_mux->nsinks] = newpad ;
	gst_dcvrtpmux_setup_sinkpad(element,newpad) ;
	ppad->pD.bufq = g_queue_new() ;
	ppad->pD.entries = 0;
	ppad->caps = caps ;
	ppad->eosRcvd = false ;
	
	dcv_rtp_mux->nsinks++ ;
	GST_LOG_OBJECT(dcv_rtp_mux,"Added new pad :%s, active pads=%u\n",
			gst_pad_get_name(newpad),dcv_rtp_mux->nsinks) ;
  }
  else
    GST_WARNING_OBJECT (dcv_rtp_mux, "failed to create request pad");

  return newpad;
}
static void gst_dcvrtpmux_setup_sinkpad(GstElement * filter, GstPad *sinkpad)
{
  gst_pad_set_event_function (sinkpad, GST_DEBUG_FUNCPTR(gst_dcvrtpmux_sink_event));
  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR(gst_dcvrtpmux_chain_rtpbuffer));
  gst_pad_set_query_function_full (sinkpad, gstQueryFunc,NULL,NULL) ;
// GST_PAD_SET_PROXY_CAPS (filter->video_in);
  gst_element_add_pad (GST_ELEMENT (filter), sinkpad);
}
static void
gst_dcvrtpmux_init (Gstdcvrtpmux * filter)
{

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
//  GST_PAD_SET_PROXY_CAPS (filter->video_out);
  gst_pad_set_query_function_full (filter->srcpad, gstQueryFunc,NULL,NULL) ;
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->silent = FALSE;
}

static int dcvrtpmuxProcessQueuesDcv(Gstdcvrtpmux * filter)
{
}

static void
gst_dcvrtpmux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstdcvrtpmux *filter = GST_DCVRTPMUX (object);
  GST_LOG_OBJECT(object,"%s: Setting property %u\n",gst_element_get_name(GST_ELEMENT(object)), prop_id) ;

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dcvrtpmux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstdcvrtpmux *filter = GST_DCVRTPMUX (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_dcvrtpmux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstdcvrtpmux *filter;
  gboolean ret;

  filter = GST_DCVRTPMUX (parent);

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
    case GST_EVENT_EOS:
    {
	guint i;
	gboolean empty = true ;
	dcvrtpmux_bufq_loc_t *ld = getQueue(parent,pad) ;
	ld->eosRcvd = true ;
	for (i=0; i<filter->nsinks ; i++)
	{
		dcvrtpmux_bufq_loc_t *ld = &(filter->padq[i]) ;
		if (!g_queue_is_empty(ld->pD.bufq)) {
			empty = false ;
			break ;
		}
	}
	if (empty) ret = gst_pad_event_default(pad, parent,event) ;
	else ret = true ;
	break ;
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
dcvrtpmux_bufq_loc_t * getQueue(GstObject *parent, GstPad *pad)
{
	guint i;
  	GstDcvRtpMux_t *filter = GST_DCVRTPMUX(parent);
	for (i=0; i<filter->nsinks; i++) {
		if (pad == filter->sinkpads[i]) {
			return &(filter->padq[i]) ;
		}
	}
	return NULL ;
}

static GstFlowReturn dcvrtpmux_ProcessQueues(GstDcvRtpMux_t *filter)
{
	static guint nextq = 0 ;
	guint qid = 0;
	for (qid = 0; qid < filter->nsinks; qid++) 
	{	
		dcvrtpmux_bufq_loc_t *ld = &filter->padq[(qid+nextq)%filter->nsinks] ;
		if (g_queue_is_empty(ld->pD.bufq)) {
			continue ;
		}
		else if (qid != 0 && (g_queue_get_length(ld->pD.bufq) < 5))
			continue ;

		GstBuffer *nbuf = g_queue_pop_head(ld->pD.bufq) ;
		if (qid == 0) nextq = (nextq + 1)%filter->nsinks ;
		GST_LOG_OBJECT(filter,"Pushing packet to src, next q is %u\n",nextq) ;
		return gst_pad_push(filter->srcpad,nbuf) ;
	}
	GST_LOG_OBJECT(filter,"There's nothing to be sent\n") ;
	return GST_FLOW_OK ;
}

gboolean dcvrtpmux_CanSendEos(GstDcvRtpMux_t *filter)
{
	int i;
	for (i=0; i<filter->nsinks; i++) {
		dcvrtpmux_bufq_loc_t *ld = &(filter->padq[i]) ;
		if (!g_queue_is_empty(ld->pD.bufq) || ld->eosRcvd == false) 
			break ;
	}
	if (i < filter->nsinks) return false ;
	else return true;
}

static GstFlowReturn gst_dcvrtpmux_chain_rtpbuffer (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn retval ;
  GstDcvRtpMux_t *filter = GST_DCVRTPMUX(parent);
  dcvrtpmux_bufq_loc_t * ld ;
  ld = getQueue(parent,pad) ;
  ld->caps = gst_pad_get_current_caps(pad) ;

  if (filter->silent == FALSE)
  {
    if (filter->dcvrtpmux_nf == 0)
    	GST_LOG_OBJECT (parent,"DCV RTP buffer rx: (%d) caps %s\n", filter->dcvrtpmux_nf,gst_caps_to_string(ld->caps)) ;
    else
    	GST_LOG_OBJECT (parent,"DCV RTP buffer rx: (%d) \n", filter->dcvrtpmux_nf) ;
  }
  filter->dcvrtpmux_nf++ ;

  g_queue_push_tail(ld->pD.bufq,buf)  ;
  if ( (retval = dcvrtpmux_ProcessQueues(filter)) == GST_FLOW_OK) {
	if (dcvrtpmux_CanSendEos(filter)) {
#if 0
		GstEvent *event = gst_event_new_eos() ;
		retval = gst_pad_push_event(filter->srcpad,event) ;
#endif
		GST_WARNING_OBJECT(parent,"Conditions achieved for sending eos\n") ;
		return retval ;
	}
	else
		return retval ;
  }
  else
	return retval ;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
dcvrtpmux_init (GstPlugin * dcvrtpmux)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template dcvrtpmux' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_dcvrtpmux_debug, "gst_dcvrtpmux",
      0, "Template dcvrtpmux");

  return gst_element_register (dcvrtpmux, "dcvrtpmux", GST_RANK_NONE,
      GST_TYPE_DCVRTPMUX);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstdcvrtpmux"
#endif

/* gstreamer looks for this structure to register dcvrtpmuxs
 *
 * exchange the string 'Template dcvrtpmux' with your dcvrtpmux description
 */
#define PACKAGE_VERSION "1.0"
#define GST_LICENSE "GPL"
#define GST_PACKAGE_NAME "dcvrtpmux"
#define GST_PACKAGE_ORIGIN "http://www.hsc.com"

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dcvrtpmux,
    "dcvrtpmux for dscope",
    dcvrtpmux_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
