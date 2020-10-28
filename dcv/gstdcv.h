/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
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

#ifndef __GST_DCV_H__
#define __GST_DCV_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_DCV (gst_dcv_get_type())
G_DECLARE_FINAL_TYPE (Gstdcv, gst_dcv,
    GST, PLUGIN_TEMPLATE, GstElement)

typedef enum {
	GRCVR_FIRST,
	GRCVR_INTERMEDIATE,
	GRCVR_LAST
} grcvr_mode_e ;


typedef struct {
	GQueue *bufq;
	struct timeval lastData;
	struct timezone tz;
	int entries ;
} dcv_bufq_t ;

typedef struct {
	dcv_bufq_t *pD ;
	GstCaps * caps ;
} dcv_bufq_loc_t  ;

typedef struct {
	GstBuffer *nb;
	GstCaps *caps;
	struct timeval ctime;
	struct timezone ctz;
} dcv_BufContainer_t ;

typedef struct {
	dcv_bufq_t dataqueue;
	dcv_bufq_t olddataqueue;
	dcv_bufq_t videoframequeue;
} dcv_data_struct_t ;

struct _Gstdcv
{
  GstElement element;

  GstPad *video_in, *rtp_in;
  GstPad *video_out, *rtp_out;

  gboolean silent;
  gpointer execFn;
  gint     grcvrMode ;
  gint     vfmatch ;
  gint     vbufsnt;
  GstCaps *vcaps ;
  GstCaps *dcaps ;
  dcvFrameData_t *Dv ;
  dcv_data_struct_t Q ;
} ;


typedef int (* dcv_fn_t )(void * img, void *dataIn, int insize, void * pointlist, int outdatasize) ;
#define GST_DCV(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DCV,Gstdcv))
#define GST_DCV_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DCV,GstdcvClass))
#define GST_IS_DCV(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DCV))
#define GST_IS_DCV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DCV))

static int dcvProcessQueuesDcv(Gstdcv * filter) ;
/* Standard function returning type information. */
GType gst_my_filter_get_type (void);
G_END_DECLS

#endif /* __GST_DCV_H__ */
