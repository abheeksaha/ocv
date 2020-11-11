/* RTP muxer element for GStreamer
 *
 * gstrtpmux.h:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
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

#ifndef __GST_DCV_RTP_MUX_H__
#define __GST_DCV_RTP_MUX_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS
#define GST_TYPE_DCV_RTP_MUX (gst_dcvrtp_mux_get_type())
#define GST_DCV_RTP_MUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DCV_RTP_MUX, GstDcvRTPMux))
#define GST_DCV_RTP_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DCV_RTP_MUX, GstDvvRTPMuxClass))
#define GST_DCV_RTP_MUX_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DCV_RTP_MUX, GstDcvRTPMuxClass))
#define GST_IS_DCV_RTP_MUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DCV_RTP_MUX))
#define GST_IS_DCV_RTP_MUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DCV_RTP_MUX))
typedef struct _GstDcvRTPMux GstDcvRTPMux;
typedef struct _GstDcvRTPMuxClass GstDcvRTPMuxClass;


typedef struct
{
  gboolean have_timestamp_offset;
  guint timestamp_offset;

  GstSegment segment;

  gboolean priority;
} GstDcvRTPMuxPadPrivate;


/**
 * GstRTPMux:
 *
 * The opaque #GstRTPMux structure.
 */
struct _GstDcvRTPMux
{
  GstElement element;

  /* sinkpads **/
  GstCollectPads *sinkpads;
  /* srcpad */
  GstPad *srcpad;
  guint active_pads;

  guint32 ts_base;
  guint16 seqnum_base;

  gint32 ts_offset;
  gint16 seqnum_offset;
  guint16 seqnum;               /* protected by object lock */
  guint ssrc;
  guint current_ssrc;
  gboolean have_ssrc;

  GstPad *last_pad; /* protected by object lock */

  GstClockTime last_stop;
  gboolean send_stream_start;
};

typedef enum
{
  GST_DCV_RTP_PAD_STATE_CONTROL = 0,
  GST_DCV_RTP_PAD_STATE_DATA = 1
}
GstDcvRtpPadState;

typedef struct
{
  GstCollectData collect;       /* we extend the CollectData */

  gboolean have_type;
  gchar padname[24] ;           /* Name the pad **/
  guint txsize ;
  GstSegment segment;

  GstBuffer *buffer;            /* the first waiting buffer for the pad */

  gint64 packetno;              /* number of next packet */
  gint64 pageno;                /* number of next page */
  guint64 duration;             /* duration of current page */
  gboolean eos;
  gint64 offset;
  GstClockTime timestamp;       /* timestamp of the first packet on the next
                                 * page to be dequeued */
  GstClockTime timestamp_end;   /* end timestamp of last complete packet on
                                   the next page to be dequeued */
  GstClockTime gp_time;         /* time corresponding to the gp value of the
                                   last complete packet on the next page to be
                                   dequeued */

  GstDcvRtpPadState state;         /* state of the pad */

  GQueue *pagebuffers;          /* List of pages in buffers ready for pushing */

  gboolean new_page;            /* starting a new page */
  gboolean first_delta;         /* was the first packet in the page a delta */
  gboolean prev_delta;          /* was the previous buffer a delta frame */
  gboolean data_pushed;         /* whether we pushed data already */

  gint64  next_granule;         /* expected granule of next buffer ts */
  gint64  keyframe_granule;     /* granule of last preceding keyframe */

  GstTagList *tags;
}
GstDcvRtpPadData;
struct _GstDcvRTPMuxClass
{
  GstElementClass parent_class;

  gboolean (*accept_buffer_locked) (GstDcvRTPMux *rtp_mux,
      GstDcvRTPMuxPadPrivate * padpriv, GstRTPBuffer * buffer);

  gboolean (*src_event) (GstDcvRTPMux *rtp_mux, GstEvent *event);
};


GType gst_dcvrtp_mux_get_type (void);
gboolean gst_dcvrtp_mux_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_RTP_MUX_H__ */
