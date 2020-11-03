/* RTP muxer element for GStreamer
 *
 * gstrtpmux.c:
 *
 * Copyright (C) <2007-2010> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2007-2010> Collabora Ltd
 *   Contact: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-rtpmux
 * @see_also: rtpdtmfmux
 *
 * The rtp muxer takes multiple RTP streams having the same clock-rate and
 * muxes into a single stream with a single SSRC.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 rtpmux name=mux ! udpsink host=127.0.0.1 port=8888        \
 *              alsasrc ! alawenc ! rtppcmapay !                        \
 *              application/x-rtp, payload=8, rate=8000 ! mux.sink_0    \
 *              audiotestsrc is-live=1 !                                \
 *              mulawenc ! rtppcmupay !                                 \
 *              application/x-rtp, payload=0, rate=8000 ! mux.sink_1
 * ]|
 * In this example, an audio stream is captured from ALSA and another is
 * generated, both are encoded into different payload types and muxed together
 * so they can be sent on the same port.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>

#include "gstdcvrtpmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_dcv_rtp_mux_debug);
#define GST_CAT_DEFAULT gst_dcv_rtp_mux_debug

enum
{
  PROP_0,
  PROP_TIMESTAMP_OFFSET,
  PROP_SEQNUM_OFFSET,
  PROP_SEQNUM,
  PROP_SSRC
};

#define DEFAULT_TIMESTAMP_OFFSET -1
#define DEFAULT_SEQNUM_OFFSET    -1
#define DEFAULT_SSRC             -1

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstPad *gst_dcv_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_dcv_rtp_mux_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn gst_dcv_rtp_mux_collect(GstCollectPads *sinkpads, GstDcvRTPMux *mux) ;
static GstFlowReturn gst_dcv_rtp_mux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstFlowReturn gst_rtp_dcv_mux_chain_list(GstPad * pad, GstObject * parent,
    GstBufferList * bufferlist);
static gboolean gst_rtp_mux_setcaps (GstPad * pad, GstDcvRTPMux * rtp_mux,
    GstCaps * caps);
static gboolean gst_dcv_rtp_mux_flush(GstCollectPads * pads,gpointer pdata) ;
static gboolean gst_dcv_rtp_mux_sink_event (GstCollectPads * pads, GstCollectData * pad, GstEvent * event, gpointer pdata);
static gboolean gst_dcv_rtp_mux_sink_query (GstCollectPads * pad, GstCollectData * cdata, GstQuery * query, gboolean discard);
static GstFlowReturn 
gst_dcv_rtp_mux_collected (GstCollectPads * pads, GstCollectData * Data, GstBuffer * buffer, GstDcvRTPMux * dcv_mux) ;

static GstStateChangeReturn gst_dcv_rtp_mux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_dcv_rtp_mux_pad_destroy_notify (GstCollectData * data) ;
static void gst_rtp_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_dispose (GObject * object);

static gboolean gst_rtp_mux_src_event_real (GstDcvRTPMux * rtp_mux,
    GstEvent * event);

G_DEFINE_TYPE (GstDcvRTPMux, gst_dcvrtp_mux, GST_TYPE_ELEMENT);


static void
gst_dcvrtp_mux_class_init (GstDcvRTPMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;


  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

  gst_element_class_set_static_metadata (gstelement_class, "RTP muxer",
      "Codec/Muxer",
      "multiplex N rtp streams into one in round robin", "Zeeshan Ali <first.last@nokia.com>");

  gobject_class->get_property = gst_rtp_mux_get_property;
  gobject_class->set_property = gst_rtp_mux_set_property;
  gobject_class->dispose = gst_rtp_mux_dispose;

  klass->src_event = gst_rtp_mux_src_event_real;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (-1 = random)", -1,
          G_MAXINT, DEFAULT_TIMESTAMP_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXINT,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (default == random)",
          0, G_MAXUINT, DEFAULT_SSRC,
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_change_state);
}

static void
gst_rtp_mux_dispose (GObject * object)
{
  GstDcvRTPMux *rtp_mux = GST_DCV_RTP_MUX (object);
  GList *item;

  g_clear_object (&rtp_mux->last_pad);

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);
    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  //object->dispose (object);
}

static gboolean
gst_rtp_mux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDcvRTPMux *rtp_mux = GST_DCV_RTP_MUX (parent);
  GstDcvRTPMuxClass *klass;
  gboolean ret;

  klass = GST_DCV_RTP_MUX_GET_CLASS (rtp_mux);

  ret = klass->src_event (rtp_mux, event);

  return ret;
}

static gboolean
gst_rtp_mux_src_event_real (GstDcvRTPMux * rtp_mux, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "GstRTPCollision")) {
        guint ssrc = 0;

        if (!gst_structure_get_uint (s, "ssrc", &ssrc))
          ssrc = -1;

        GST_DEBUG_OBJECT (rtp_mux, "collided ssrc: %x", ssrc);

        /* choose another ssrc for our stream */
        GST_OBJECT_LOCK (rtp_mux);
        if (ssrc == rtp_mux->current_ssrc) {
          GstCaps *caps;
          guint suggested_ssrc = 0;
          guint32 new_ssrc;

          if (gst_structure_get_uint (s, "suggested-ssrc", &suggested_ssrc))
            rtp_mux->current_ssrc = suggested_ssrc;

          while (ssrc == rtp_mux->current_ssrc)
            rtp_mux->current_ssrc = g_random_int ();

          new_ssrc = rtp_mux->current_ssrc;
          GST_INFO_OBJECT (rtp_mux, "New ssrc after collision %x (was: %x)",
              new_ssrc, ssrc);
          GST_OBJECT_UNLOCK (rtp_mux);

          caps = gst_pad_get_current_caps (rtp_mux->srcpad);
          caps = gst_caps_make_writable (caps);
          gst_caps_set_simple (caps, "ssrc", G_TYPE_UINT, new_ssrc, NULL);
          gst_pad_set_caps (rtp_mux->srcpad, caps);
          gst_caps_unref (caps);
        } else {
          GST_OBJECT_UNLOCK (rtp_mux);
        }
      }
      break;
    }
    default:
      break;
  }


  return gst_pad_event_default (rtp_mux->srcpad, GST_OBJECT (rtp_mux), event);
}

static void
gst_dcvrtp_mux_init (GstDcvRTPMux * dcv_rtp_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (dcv_rtp_mux);

  dcv_rtp_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (dcv_rtp_mux->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_src_event));
  gst_pad_use_fixed_caps (dcv_rtp_mux->srcpad);
  gst_element_add_pad (GST_ELEMENT (dcv_rtp_mux), dcv_rtp_mux->srcpad);

  dcv_rtp_mux->ssrc = DEFAULT_SSRC;
  dcv_rtp_mux->current_ssrc = DEFAULT_SSRC;
  dcv_rtp_mux->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  dcv_rtp_mux->seqnum_offset = DEFAULT_SEQNUM_OFFSET;

  dcv_rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

  dcv_rtp_mux->sinkpads = gst_collect_pads_new() ;
  gst_collect_pads_set_buffer_function (dcv_rtp_mux->sinkpads, GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_collected),dcv_rtp_mux);
  gst_collect_pads_set_event_function (dcv_rtp_mux->sinkpads, GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_sink_event),dcv_rtp_mux);
  gst_collect_pads_set_flush_function (dcv_rtp_mux->sinkpads, GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_flush),dcv_rtp_mux);
}

static void
gst_dcv_rtp_mux_pad_destroy_notify (GstCollectData * data)
{
  GstDcvRtpPadData *dcvpad = (GstDcvRtpPadData *) data;
  g_print("Destroy notify called\n") ;
}
static void
gst_rtp_mux_setup_sinkpad (GstDcvRTPMux * rtp_mux, GstPad * sinkpad)
{
#if 0
  GstDcvRTPMuxPadPrivate *padpriv = g_slice_new0 (GstDcvRTPMuxPadPrivate);

  /* setup some pad functions */
  
  gst_pad_set_chain_list_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_chain_list_collected));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_sink_event));
  gst_pad_set_query_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_dcv_rtp_mux_sink_query));


  gst_pad_set_element_private (sinkpad, padpriv);

  gst_pad_set_active (sinkpad, TRUE);
  gst_collect_pads_add_pad (rtp_mux->sinkpads, sinkpad,
          sizeof (GstDcvRtpPadData), gst_dcv_rtp_mux_dcv_pad_destroy_notify, FALSE);
      dcv_mux->active_pads++;
#endif
}

static GstPad *
gst_dcv_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstDcvRTPMux *dcv_rtp_mux;
  GstPad *newpad;

  GST_LOG_OBJECT(element,"Request received for pad %s, caps:%s\n",req_name,gst_caps_to_string(caps)) ;
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_DCV_RTP_MUX (element), NULL);

  dcv_rtp_mux = GST_DCV_RTP_MUX (element);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING_OBJECT (dcv_rtp_mux, "request pad that is not a SINK pad");
    return NULL;
  }

  newpad = gst_pad_new_from_template (templ, req_name);
  if (newpad)
  {
	GstDcvRtpPadData *dcvpad = (GstDcvRtpPadData *)gst_collect_pads_add_pad(dcv_rtp_mux->sinkpads,newpad,
			  sizeof(GstDcvRtpPadData), gst_dcv_rtp_mux_pad_destroy_notify, FALSE) ;

	dcv_rtp_mux->active_pads++ ;
    	gst_rtp_mux_setup_sinkpad (dcv_rtp_mux, newpad);
	gst_element_add_pad(element,newpad) ;
  }
  else
    GST_WARNING_OBJECT (dcv_rtp_mux, "failed to create request pad");

  return newpad;
}

static void
gst_dcv_rtp_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstDcvRTPMux * dcv_rtp_mux = GST_DCV_RTP_MUX (element);
  GstDcvRTPMuxPadPrivate *padpriv;


  GST_OBJECT_LOCK (element);
  padpriv = gst_pad_get_element_private (pad);
  gst_pad_set_element_private (pad, NULL);
  GST_OBJECT_UNLOCK (element);

  gst_collect_pads_remove_pad(dcv_rtp_mux->sinkpads, pad);
  gst_element_remove_pad (element,pad) ;

  if (padpriv) {
    g_slice_free (GstDcvRTPMuxPadPrivate, padpriv);
  }
}

/* Put our own timestamp-offset on the buffer */
static void
gst_rtp_mux_readjust_rtp_timestamp_locked (GstDcvRTPMux * rtp_mux,
    GstDcvRTPMuxPadPrivate * padpriv, GstRTPBuffer * rtpbuffer)
{
  guint32 ts;
  guint32 sink_ts_base = 0;

  if (padpriv && padpriv->have_timestamp_offset)
    sink_ts_base = padpriv->timestamp_offset;

  ts = gst_rtp_buffer_get_timestamp (rtpbuffer) - sink_ts_base +
      rtp_mux->ts_base;
  GST_LOG_OBJECT (rtp_mux, "Re-adjusting RTP ts %u to %u",
      gst_rtp_buffer_get_timestamp (rtpbuffer), ts);
  gst_rtp_buffer_set_timestamp (rtpbuffer, ts);
}

static gboolean
process_buffer_locked (GstDcvRTPMux * rtp_mux, GstDcvRTPMuxPadPrivate * padpriv,
    GstRTPBuffer * rtpbuffer)
{
  GstDcvRTPMuxClass *klass = GST_DCV_RTP_MUX_GET_CLASS (rtp_mux);

  if (klass->accept_buffer_locked)
    if (!klass->accept_buffer_locked (rtp_mux, padpriv, rtpbuffer))
      return FALSE;

  rtp_mux->seqnum++;
  gst_rtp_buffer_set_seq (rtpbuffer, rtp_mux->seqnum);

  gst_rtp_buffer_set_ssrc (rtpbuffer, rtp_mux->current_ssrc);
  gst_rtp_mux_readjust_rtp_timestamp_locked (rtp_mux, padpriv, rtpbuffer);
  GST_LOG_OBJECT (rtp_mux,
      "Pushing packet size %" G_GSIZE_FORMAT ", seq=%d, ts=%u, ssrc=%x",
      rtpbuffer->map[0].size, rtp_mux->seqnum,
      gst_rtp_buffer_get_timestamp (rtpbuffer), rtp_mux->current_ssrc);

  if (padpriv) {
    if (padpriv->segment.format == GST_FORMAT_TIME) {
      GST_BUFFER_PTS (rtpbuffer->buffer) =
          gst_segment_to_running_time (&padpriv->segment, GST_FORMAT_TIME,
          GST_BUFFER_PTS (rtpbuffer->buffer));
      GST_BUFFER_DTS (rtpbuffer->buffer) =
          gst_segment_to_running_time (&padpriv->segment, GST_FORMAT_TIME,
          GST_BUFFER_DTS (rtpbuffer->buffer));
    }
  }

  return TRUE;
}

struct BufferListData
{
  GstDcvRTPMux *rtp_mux;
  GstDcvRTPMuxPadPrivate *padpriv;
  gboolean drop;
};

static gboolean
process_list_item (GstBuffer ** buffer, guint idx, gpointer user_data)
{
  struct BufferListData *bd = user_data;
  GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

  *buffer = gst_buffer_make_writable (*buffer);

  gst_rtp_buffer_map (*buffer, GST_MAP_READWRITE, &rtpbuffer);

  bd->drop = !process_buffer_locked (bd->rtp_mux, bd->padpriv, &rtpbuffer);

  gst_rtp_buffer_unmap (&rtpbuffer);

  if (bd->drop)
    return FALSE;

  if (GST_BUFFER_DURATION_IS_VALID (*buffer) &&
      GST_BUFFER_PTS_IS_VALID (*buffer))
    bd->rtp_mux->last_stop = GST_BUFFER_PTS (*buffer) +
        GST_BUFFER_DURATION (*buffer);
  else
    bd->rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean resend_events (GstPad * pad, GstEvent ** event,
    gpointer user_data);

static GstFlowReturn
gst_dcv_rtp_mux_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * bufferlist)
{
  GstDcvRTPMux *rtp_mux;
  GstFlowReturn ret;
  GstDcvRTPMuxPadPrivate *padpriv;
  gboolean changed = FALSE;
  struct BufferListData bd;

  rtp_mux = GST_DCV_RTP_MUX (parent);

  if (gst_pad_check_reconfigure (rtp_mux->srcpad)) {
    GstCaps *current_caps = gst_pad_get_current_caps (pad);

    if (!gst_rtp_mux_setcaps (pad, rtp_mux, current_caps)) {
      gst_pad_mark_reconfigure (rtp_mux->srcpad);
      if (GST_PAD_IS_FLUSHING (rtp_mux->srcpad))
        ret = GST_FLOW_FLUSHING;
      else
        ret = GST_FLOW_NOT_NEGOTIATED;
      gst_buffer_list_unref (bufferlist);
      goto out;
    }
    gst_caps_unref (current_caps);
  }

  GST_OBJECT_LOCK (rtp_mux);

  padpriv = gst_pad_get_element_private (pad);
  if (!padpriv) {
    GST_OBJECT_UNLOCK (rtp_mux);
    ret = GST_FLOW_NOT_LINKED;
    gst_buffer_list_unref (bufferlist);
    goto out;
  }

  bd.rtp_mux = rtp_mux;
  bd.padpriv = padpriv;
  bd.drop = FALSE;

  bufferlist = gst_buffer_list_make_writable (bufferlist);
  gst_buffer_list_foreach (bufferlist, process_list_item, &bd);

  if (!bd.drop && pad != rtp_mux->last_pad) {
    changed = TRUE;
    g_clear_object (&rtp_mux->last_pad);
    rtp_mux->last_pad = g_object_ref (pad);
  }

  GST_OBJECT_UNLOCK (rtp_mux);

  if (changed)
    gst_pad_sticky_events_foreach (pad, resend_events, rtp_mux);

  if (bd.drop) {
    gst_buffer_list_unref (bufferlist);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_pad_push_list (rtp_mux->srcpad, bufferlist);
  }

out:

  return ret;
}

static gboolean
resend_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstDcvRTPMux *rtp_mux = user_data;

  if (GST_EVENT_TYPE (*event) == GST_EVENT_CAPS) {
    GstCaps *caps;

    gst_event_parse_caps (*event, &caps);
    gst_rtp_mux_setcaps (pad, rtp_mux, caps);
  } else if (GST_EVENT_TYPE (*event) == GST_EVENT_SEGMENT) {
    GstSegment new_segment;
    gst_segment_init (&new_segment, GST_FORMAT_TIME);
    gst_pad_push_event (rtp_mux->srcpad, gst_event_new_segment (&new_segment));
  } else {
    gst_pad_push_event (rtp_mux->srcpad, gst_event_ref (*event));
  }

  return TRUE;
}

static GstFlowReturn
gst_dcv_rtp_mux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstDcvRTPMux *rtp_mux;
  GstFlowReturn ret;
  GstDcvRTPMuxPadPrivate *padpriv;
  gboolean drop;
  gboolean changed = FALSE;
  GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

  rtp_mux = GST_DCV_RTP_MUX (parent);

  if (gst_pad_check_reconfigure (rtp_mux->srcpad)) {
    GstCaps *current_caps = gst_pad_get_current_caps (pad);

    if (!gst_rtp_mux_setcaps (pad, rtp_mux, current_caps)) {
      gst_pad_mark_reconfigure (rtp_mux->srcpad);
      if (GST_PAD_IS_FLUSHING (rtp_mux->srcpad))
        ret = GST_FLOW_FLUSHING;
      else
        ret = GST_FLOW_NOT_NEGOTIATED;
      gst_buffer_unref (buffer);
      goto out;
    }
    gst_caps_unref (current_caps);
  }

  GST_OBJECT_LOCK (rtp_mux);
  padpriv = gst_pad_get_element_private (pad);

  if (!padpriv) {
    GST_OBJECT_UNLOCK (rtp_mux);
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_LINKED;
  }

  buffer = gst_buffer_make_writable (buffer);

  if (!gst_rtp_buffer_map (buffer, GST_MAP_READWRITE, &rtpbuffer)) {
    GST_OBJECT_UNLOCK (rtp_mux);
    gst_buffer_unref (buffer);
    GST_ERROR_OBJECT (rtp_mux, "Invalid RTP buffer");
    return GST_FLOW_ERROR;
  }

  drop = !process_buffer_locked (rtp_mux, padpriv, &rtpbuffer);

  gst_rtp_buffer_unmap (&rtpbuffer);

  if (!drop) {
    if (pad != rtp_mux->last_pad) {
      changed = TRUE;
      g_clear_object (&rtp_mux->last_pad);
      rtp_mux->last_pad = g_object_ref (pad);
    }

    if (GST_BUFFER_DURATION_IS_VALID (buffer) &&
        GST_BUFFER_PTS_IS_VALID (buffer))
      rtp_mux->last_stop = GST_BUFFER_PTS (buffer) +
          GST_BUFFER_DURATION (buffer);
    else
      rtp_mux->last_stop = GST_CLOCK_TIME_NONE;
  }

  GST_OBJECT_UNLOCK (rtp_mux);

  if (changed)
    gst_pad_sticky_events_foreach (pad, resend_events, rtp_mux);

  if (drop) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_pad_push (rtp_mux->srcpad, buffer);
  }

out:
  return ret;
}

static gboolean
gst_rtp_mux_setcaps (GstPad * pad, GstDcvRTPMux * rtp_mux, GstCaps * caps)
{
  GstStructure *structure;
  gboolean ret = FALSE;
  GstDcvRTPMuxPadPrivate *padpriv;
  GstCaps *peercaps;

  if (caps == NULL)
    return FALSE;

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  peercaps = gst_pad_peer_query_caps (rtp_mux->srcpad, NULL);
  if (peercaps) {
    GstCaps *tcaps, *othercaps;;
    tcaps = gst_pad_get_pad_template_caps (pad);
    othercaps = gst_caps_intersect_full (peercaps, tcaps,
        GST_CAPS_INTERSECT_FIRST);

    if (gst_caps_get_size (othercaps) > 0) {
      structure = gst_caps_get_structure (othercaps, 0);
      GST_OBJECT_LOCK (rtp_mux);
      if (gst_structure_get_uint (structure, "ssrc", &rtp_mux->current_ssrc)) {
        GST_INFO_OBJECT (pad, "Use downstream ssrc: %x", rtp_mux->current_ssrc);
        rtp_mux->have_ssrc = TRUE;
      }
      if (gst_structure_get_uint (structure,
              "timestamp-offset", &rtp_mux->ts_base)) {
        GST_INFO_OBJECT (pad, "Use downstream timestamp-offset: %u",
            rtp_mux->ts_base);
      }
      GST_OBJECT_UNLOCK (rtp_mux);
    }

    gst_caps_unref (othercaps);

    gst_caps_unref (peercaps);
    gst_caps_unref (tcaps);
  }

  structure = gst_caps_get_structure (caps, 0);

  if (!structure)
    return FALSE;

  GST_OBJECT_LOCK (rtp_mux);
  padpriv = gst_pad_get_element_private (pad);
  if (padpriv &&
      gst_structure_get_uint (structure, "timestamp-offset",
          &padpriv->timestamp_offset)) {
    padpriv->have_timestamp_offset = TRUE;
  }

  caps = gst_caps_copy (caps);

  /* if we don't have a specified ssrc, first try to take one from the caps,
     and if that fails, generate one */
  if (rtp_mux->ssrc == DEFAULT_SSRC) {
    if (rtp_mux->current_ssrc == DEFAULT_SSRC) {
      if (!gst_structure_get_uint (structure, "ssrc", &rtp_mux->current_ssrc)) {
        rtp_mux->current_ssrc = g_random_int ();
        GST_INFO_OBJECT (rtp_mux, "Set random ssrc %x", rtp_mux->current_ssrc);
      }
    }
  } else {
    rtp_mux->current_ssrc = rtp_mux->ssrc;
    GST_INFO_OBJECT (rtp_mux, "Set ssrc %x", rtp_mux->current_ssrc);
  }

  gst_caps_set_simple (caps,
      "timestamp-offset", G_TYPE_UINT, rtp_mux->ts_base,
      "seqnum-offset", G_TYPE_UINT, rtp_mux->seqnum_base,
      "ssrc", G_TYPE_UINT, rtp_mux->current_ssrc, NULL);

  GST_OBJECT_UNLOCK (rtp_mux);

  if (rtp_mux->send_stream_start) {
    gchar s_id[32];

    /* stream-start (FIXME: create id based on input ids) */
    g_snprintf (s_id, sizeof (s_id), "interleave-%08x", g_random_int ());
    gst_pad_push_event (rtp_mux->srcpad, gst_event_new_stream_start (s_id));

    rtp_mux->send_stream_start = FALSE;
  }

  GST_DEBUG_OBJECT (rtp_mux,
      "setting caps %" GST_PTR_FORMAT " on src pad..", caps);
  ret = gst_pad_set_caps (rtp_mux->srcpad, caps);


  gst_caps_unref (caps);

  return ret;
}

static void
clear_caps (GstCaps * caps, gboolean only_clock_rate)
{
  gint i, j;

  /* Lets only match on the clock-rate */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    for (j = 0; j < gst_structure_n_fields (s); j++) {
      const gchar *name = gst_structure_nth_field_name (s, j);

      if (strcmp (name, "clock-rate") && (only_clock_rate ||
              (strcmp (name, "ssrc")))) {
        gst_structure_remove_field (s, name);
        j--;
      }
    }
  }
}

static gboolean
same_clock_rate_fold (const GValue * item, GValue * ret, gpointer user_data)
{
  GstPad *mypad = user_data;
  GstPad *pad = g_value_get_object (item);
  GstCaps *peercaps;
  GstCaps *accumcaps;

  if (pad == mypad)
    return TRUE;

  accumcaps = g_value_get_boxed (ret);
  peercaps = gst_pad_peer_query_caps (pad, accumcaps);
  if (!peercaps) {
    g_warning ("no peercaps");
    return TRUE;
  }
  peercaps = gst_caps_make_writable (peercaps);
  clear_caps (peercaps, TRUE);

  g_value_take_boxed (ret, peercaps);

  return !gst_caps_is_empty (peercaps);
}

static GstCaps *
gst_rtp_mux_getcaps (GstPad * pad, GstDcvRTPMux * mux, GstCaps * filter)
{
  GstCaps *caps = NULL;
  GstIterator *iter = NULL;
  GValue v = { 0 };
  GstIteratorResult res;
  GstCaps *peercaps;
  GstCaps *othercaps;
  GstCaps *tcaps;
  const GstStructure *structure;

  peercaps = gst_pad_peer_query_caps (mux->srcpad, NULL);

  if (peercaps) {
    tcaps = gst_pad_get_pad_template_caps (pad);
    othercaps = gst_caps_intersect_full (peercaps, tcaps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
  } else {
    tcaps = gst_pad_get_pad_template_caps (mux->srcpad);
    if (filter)
      othercaps = gst_caps_intersect_full (filter, tcaps,
          GST_CAPS_INTERSECT_FIRST);
    else
      othercaps = gst_caps_copy (tcaps);
  }
  gst_caps_unref (tcaps);

  GST_LOG_OBJECT (pad, "Intersected srcpad-peercaps and template caps: %"
      GST_PTR_FORMAT, othercaps);

  structure = gst_caps_get_structure (othercaps, 0);
  if (mux->ssrc == DEFAULT_SSRC) {
    if (gst_structure_get_uint (structure, "ssrc", &mux->current_ssrc))
      GST_DEBUG_OBJECT (pad, "Use downstream ssrc: %x", mux->current_ssrc);
  }

  clear_caps (othercaps, TRUE);

  g_value_init (&v, GST_TYPE_CAPS);

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (mux));
  do {
    gst_value_set_caps (&v, othercaps);
    res = gst_iterator_fold (iter, same_clock_rate_fold, &v, pad);
    gst_iterator_resync (iter);
  } while (res == GST_ITERATOR_RESYNC);
  gst_iterator_free (iter);

  caps = gst_caps_intersect ((GstCaps *) gst_value_get_caps (&v), othercaps);

  g_value_unset (&v);
  gst_caps_unref (othercaps);

  if (res == GST_ITERATOR_ERROR) {
    gst_caps_unref (caps);
    caps = gst_caps_new_empty ();
  }


  return caps;
}

static gboolean
gst_dcv_rtp_mux_sink_query (GstCollectPads * pads, GstCollectData * cdata, GstQuery * query,gboolean discard)
{
  gboolean res = FALSE;
    res = gst_collect_pads_query_default (pads, cdata, query,discard);

#if 0
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      GST_LOG_OBJECT (pad, "Received caps-query with filter-caps: %"
          GST_PTR_FORMAT, filter);
      caps = gst_rtp_mux_getcaps (pad, mux, filter);
      gst_query_set_caps_result (query, caps);
      GST_LOG_OBJECT (mux, "Answering caps-query with caps: %"
          GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
#endif
  return res;
}

static void
gst_rtp_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstDcvRTPMux *rtp_mux;

  rtp_mux = GST_DCV_RTP_MUX (object);

  GST_OBJECT_LOCK (rtp_mux);
  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int (value, rtp_mux->ts_offset);
      break;
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, rtp_mux->seqnum_offset);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, rtp_mux->seqnum);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, rtp_mux->ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (rtp_mux);
}

static void
gst_rtp_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstDcvRTPMux *rtp_mux;

  rtp_mux = GST_DCV_RTP_MUX (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      rtp_mux->ts_offset = g_value_get_int (value);
      break;
    case PROP_SEQNUM_OFFSET:
      rtp_mux->seqnum_offset = g_value_get_int (value);
      break;
    case PROP_SSRC:
      GST_OBJECT_LOCK (rtp_mux);
      rtp_mux->ssrc = g_value_get_uint (value);
      rtp_mux->current_ssrc = rtp_mux->ssrc;
      rtp_mux->have_ssrc = TRUE;
      GST_DEBUG_OBJECT (rtp_mux, "ssrc prop set to %x", rtp_mux->ssrc);
      GST_OBJECT_UNLOCK (rtp_mux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean gst_dcv_rtp_mux_flush(GstCollectPads * pads,gpointer pdata)
{
	GST_LOG_OBJECT(pads,"flushing function called on sinkpad\n") ;
	return TRUE ;
}
static gboolean
gst_dcv_rtp_mux_sink_event (GstCollectPads * pads, GstCollectData * pad, GstEvent * event, gpointer pdata)
{
  GstDcvRTPMux *mux = GST_DCV_RTP_MUX (pdata);
  gboolean ret;

  GST_LOG_OBJECT (pad->pad, "Got %s event", GST_EVENT_TYPE_NAME (event));
  GST_LOG_OBJECT(mux, "sink pad status:%u src pad active:%s\n",
		  GST_COLLECT_PADS_STATE(mux->sinkpads),
		  gst_pad_is_active(mux->srcpad) ? "Yes":"No") ;
#if 1

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      GST_LOG_OBJECT (pad, "Received caps-event with caps: %"
          GST_PTR_FORMAT, caps);
      ret = TRUE ;
      //ret = gst_dcv_rtp_mux_setcaps ( mux, caps);
      gst_event_unref (event);
      return ret;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      break;
    }
    default:
      break;
  }
#endif
  if (mux->srcpad) {
    return gst_pad_push_event (mux->srcpad, event);
  } else {
    return gst_collect_pads_event_default (pads, pad, event, FALSE);
  }
}

static void
gst_rtp_mux_ready_to_paused (GstDcvRTPMux * rtp_mux)
{

  GST_OBJECT_LOCK (rtp_mux);

  g_clear_object (&rtp_mux->last_pad);
  rtp_mux->send_stream_start = TRUE;

  if (rtp_mux->seqnum_offset == -1)
    rtp_mux->seqnum_base = g_random_int_range (0, G_MAXUINT16);
  else
    rtp_mux->seqnum_base = rtp_mux->seqnum_offset;
  rtp_mux->seqnum = rtp_mux->seqnum_base;

  if (rtp_mux->ts_offset == -1)
    rtp_mux->ts_base = g_random_int ();
  else
    rtp_mux->ts_base = rtp_mux->ts_offset;

  rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

  if (rtp_mux->have_ssrc)
    rtp_mux->current_ssrc = rtp_mux->ssrc;

  GST_DEBUG_OBJECT (rtp_mux, "set timestamp-offset to %u", rtp_mux->ts_base);

  GST_OBJECT_UNLOCK (rtp_mux);
}

static GstStateChangeReturn
gst_dcv_rtp_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstDcvRTPMux *rtp_mux;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  rtp_mux = GST_DCV_RTP_MUX (element);

  GST_LOG_OBJECT(element, "state transition request:%u\n",transition) ;
  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_mux_ready_to_paused (rtp_mux);
      gst_collect_pads_start (rtp_mux->sinkpads);
      gst_collect_pads_set_flushing (rtp_mux->sinkpads,FALSE);
      if (gst_pad_set_active(rtp_mux->srcpad,TRUE) != TRUE) {
	      GST_LOG_OBJECT(rtp_mux->srcpad, "Couldn't activate\n") ;
      }
      break;

  //ret = GST_ELEMENT_CLASS (parent_class)->change_state (element,
   //   transition);

    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_clear_object (&rtp_mux->last_pad);
      gst_collect_pads_start (rtp_mux->sinkpads);
      gst_collect_pads_set_flushing (rtp_mux->sinkpads,FALSE);
      if (gst_pad_set_active(rtp_mux->srcpad,TRUE) != TRUE) {
	      GST_LOG_OBJECT(rtp_mux->srcpad, "Couldn't activate\n") ;
      }
      break;
    default:
      break;
  }
  GST_LOG_OBJECT(element, "sink pad status:%u src pad active:%s\n",
		  GST_COLLECT_PADS_STATE(rtp_mux->sinkpads),
		  gst_pad_is_active(rtp_mux->srcpad) ? "Yes":"No") ;
  return ret;
}

gboolean
gst_dcvrtp_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dcv_rtp_mux_debug, "dcvrtpmux", 0, "rtp muxer");

  return gst_element_register (plugin, "dcvrtpmux", GST_RANK_NONE,
      GST_TYPE_DCV_RTP_MUX);
}

static GstFlowReturn
gst_dcv_rtp_mux_collected (GstCollectPads * pads, GstCollectData * Data, GstBuffer * buffer, GstDcvRTPMux * dcv_mux)
{

  GST_LOG_OBJECT (dcv_mux, "collected");
  return GST_FLOW_OK;
}
/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "dcvrtpmux"
#endif

/* gstreamer looks for this structure to register dcvs
 *
 * exchange the string 'Template dcv' with your dcv description
 */
#define PACKAGE_VERSION "1.0"
#define GST_LICENSE "GPL"
#define GST_PACKAGE_NAME "dcvrtpmux"
#define GST_PACKAGE_ORIGIN "http://www.hsc.com"

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dcvrtpmux,
    "dcv muxer for dscope",
    gst_dcvrtp_mux_plugin_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
