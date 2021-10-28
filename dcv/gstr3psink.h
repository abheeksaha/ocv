/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_R3P_SINK_H__
#define __GST_R3P_SINK_H__


#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include <gio/gio.h>

G_BEGIN_DECLS

#define GST_TYPE_R3P_SINK \
  (gst_r3p_sink_get_type())
#define GST_R3P_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_R3P_SINK,GstR3PSink))
#define GST_R3P_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_R3P_SINK,GstR3PSinkClass))
#define GST_IS_R3P_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_R3P_SINK))
#define GST_IS_R3P_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_R3P_SINK))

typedef struct _GstR3PSink GstR3PSink;
typedef struct _GstR3PSinkClass GstR3PSinkClass;

typedef enum {
  GST_R3P_SINK_OPEN             = (GST_ELEMENT_FLAG_LAST << 0),

  GST_R3P_SINK_FLAG_LAST        = (GST_ELEMENT_FLAG_LAST << 2),
} GstR3PSinkFlags;

struct _GstR3PSink {
  GstBaseSink element;

  /* server information */
  int port;
  gchar *host;
  int timeout ;

  /* socket */
  GSocket *socket;
  GCancellable *cancellable;

  size_t data_written; /* how much bytes have we written ? */
};

struct _GstR3PSinkClass {
  GstBaseSinkClass parent_class;
};

GType gst_r3p_sink_get_type(void);

G_END_DECLS

#endif /* __GST_R3P_SINK_H__ */
