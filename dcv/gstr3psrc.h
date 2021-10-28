/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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


#ifndef __GST_R3P_SRC_H__
#define __GST_R3P_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gio/gio.h>

G_BEGIN_DECLS


#define GST_TYPE_R3P_SRC \
  (gst_r3p_src_get_type())
#define GST_R3P_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_R3P_SRC,GstR3PSrc))
#define GST_R3P_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_R3P_SRC,GstR3PSrcClass))
#define GST_IS_R3P_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_R3P_SRC))
#define GST_IS_R3P_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_R3P_SRC))

typedef struct _GstR3PSrc GstR3PSrc;
typedef struct _GstR3PSrcClass GstR3PSrcClass;

typedef enum {
  GST_R3P_SRC_OPEN       = (GST_BASE_SRC_FLAG_LAST << 0),

  GST_R3P_SRC_FLAG_LAST  = (GST_BASE_SRC_FLAG_LAST << 2)
} GstR3PSrcFlags;

struct _GstR3PSrc {
  GstPushSrc element;

  /* server information */
  int current_port;        /* currently bound-to port, or 0 */ /* ATOMIC */
  int server_port;         /* port property */
  gchar *host;             /* host property */
  int timeout;

  GSocket *client_socket ;
  GSocket *server_socket ;
  GCancellable *cancellable;
  GError *err ;
};

struct _GstR3PSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_r3p_src_get_type (void);

G_END_DECLS

#endif /* __GST_R3P_SRC_H__ */
