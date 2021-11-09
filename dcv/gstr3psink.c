/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2011> Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * License along with thisobj library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-r3psink
 * @title: r3psink
 * @see_also: #r3psink
 *
 * ## Example launch line (server):
 * |[
 * nc -l -p 3000
 * ]|
 * ## Example launch line ():
 * |[
 * gst-launch-1.0 fdsink fd=1 ! r3psink port=3000
 * ]|
 *  everything you type in the  is shown on the server (fd=1 means
 * standard input which is the command line input file descriptor)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gstr3psink.h"
#include "tcptrans.h"

GST_DEBUG_CATEGORY_STATIC (r3psink_debug);
#define GST_CAT_DEFAULT r3psink_debug
#define R3P_DEFAULT_HOST "localhost"
#define R3P_DEFAULT_PORT 50018
#define R3P_HIGHEST_PORT 50058

/* R3PSink signals and args */
enum
{
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};


enum
{
  PROP_0,
  PROP_HOST,
  PROP_TIMEOUT,
  PROP_PORT
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_r3p_sink_finalize (GObject * gobject);

static gboolean gst_r3p_sink_setcaps (GstBaseSink * bsink,
    GstCaps * caps);
static GstFlowReturn gst_r3p_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_r3p_sink_render_list (GstBaseSink * bsink,
    GstBufferList * lbuf);
static gboolean gst_r3p_sink_start (GstBaseSink * bsink);
static gboolean gst_r3p_sink_stop (GstBaseSink * bsink);
static gboolean gst_r3p_sink_unlock (GstBaseSink * bsink);
static gboolean gst_r3p_sink_unlock_stop (GstBaseSink * bsink);

static void gst_r3p_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_r3p_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


/*static guint gst_r3p_sink_signals[LAST_SIGNAL] = { 0 }; */

#define gst_r3p_sink_parent_class parent_class
G_DEFINE_TYPE (GstR3PSink, gst_r3p_sink, GST_TYPE_BASE_SINK);

static void
gst_r3p_sink_class_init (GstR3PSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_r3p_sink_set_property;
  gobject_class->get_property = gst_r3p_sink_get_property;
  gobject_class->finalize = gst_r3p_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host", "The host/IP to send the packets to",
          R3P_DEFAULT_HOST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "Port", "The port to send the packets to",
          0, R3P_HIGHEST_PORT, R3P_DEFAULT_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_int ("timeout", "Timeout",
          "Timeout in milliseconds",
          0, 12000, 500,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_static_metadata (gstelement_class,
      "Modified R3P  sink", "Sink/Network",
      "Send data as a  over the network via R3P (request/response)",
      "Abheek Saha");

  gstbasesink_class->start = gst_r3p_sink_start;
  gstbasesink_class->stop = gst_r3p_sink_stop;
  gstbasesink_class->set_caps = gst_r3p_sink_setcaps;
  gstbasesink_class->render = gst_r3p_sink_render;
  gstbasesink_class->render_list = gst_r3p_sink_render_list;
  gstbasesink_class->unlock = gst_r3p_sink_unlock;
  gstbasesink_class->unlock_stop = gst_r3p_sink_unlock_stop;

  GST_DEBUG_CATEGORY_INIT (r3psink_debug, "r3psink", 0, "R3P Client Sink");
}

static void
gst_r3p_sink_init (GstR3PSink * thisobj)
{
  GST_FIXME_OBJECT (thisobj, "Executing Init Function");
  thisobj->host = g_strdup (R3P_DEFAULT_HOST);
  thisobj->port = R3P_DEFAULT_PORT;

  thisobj->socket = NULL;
  thisobj->cancellable = g_cancellable_new ();

  GST_OBJECT_FLAG_UNSET (thisobj, GST_R3P_SINK_OPEN);
}

static void
gst_r3p_sink_finalize (GObject * gobject)
{
  GstR3PSink *thisobj = GST_R3P_SINK (gobject);
  GST_FIXME_OBJECT (gobject, "Executing Finalize Function");

  if (thisobj->cancellable)
    g_object_unref (thisobj->cancellable);
  thisobj->cancellable = NULL;

  if (thisobj->socket)
    g_object_unref (thisobj->socket);
  thisobj->socket = NULL;

  g_free (thisobj->host);
  thisobj->host = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static gboolean
gst_r3p_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  return TRUE;
}

static GstFlowReturn 
gst_r3p_sink_render_list(GstBaseSink *bsink, GstBufferList *lbuf)
{
	guint i,numBuffers ;
	GstFlowReturn rv = GST_FLOW_OK ;
	GST_OBJECT_LOCK(GST_OBJECT(bsink)) ;
	numBuffers = gst_buffer_list_length(lbuf) ;
	for (i=0; i<numBuffers;i++) {
		GstBuffer *nb = gst_buffer_list_get(lbuf,i) ;
		rv = gst_r3p_sink_render(bsink,nb) ;
		if (rv != GST_FLOW_OK) {
  			GST_LOG_OBJECT (bsink, "Render failed for buffer %u, retval=%d",i,rv);
			break ;
		}
			
	} 
  	GST_LOG_OBJECT (bsink, "Rendered %u buffers from list (numbuffers=%u)",i,numBuffers);
	GST_OBJECT_UNLOCK(GST_OBJECT(bsink)) ;
	g_assert (rv == GST_FLOW_OK) ;
	return rv;
}

static GstFlowReturn
gst_r3p_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstR3PSink *sink;
  GstMapInfo map;
  gsize written = 0;
  gssize rret = 0;
  GError *err = NULL;

  sink = GST_R3P_SINK (bsink);

  g_return_val_if_fail (GST_OBJECT_FLAG_IS_SET (sink, GST_R3P_SINK_OPEN),
      GST_FLOW_FLUSHING);

  gst_buffer_map (buf, &map, GST_MAP_READ);
  GST_LOG_OBJECT (sink, "writing %" G_GSIZE_FORMAT " bytes for buffer data", map.size);

  /* write buffer data */
#if 0
  while (written < map.size) {
    rret =
        g_socket_send (sink->socket, (gchar *) map.data + written,
        map.size - written, sink->cancellable, &err);
    if (rret < 0)
      goto write_error;
    written += rret;
  }
#endif
  g_assert(map.size <= 99999) ;
  {
	char hdr[14] ;
	sprintf(hdr,"RCV MSG %5d\n",map.size) ;
	if ((rret = g_socket_send(sink->socket,hdr,14,sink->cancellable,&err)) != 14) {
		rret = -1;
	}
	if (err != NULL) g_error_free(err) ;
	if ((rret = g_socket_receive_with_blocking(sink->socket,hdr,3,TRUE,sink->cancellable,&err)) != 3) {
		rret = -1 ;
	}
	if (err != NULL) g_error_free(err) ;
  	if (rret < 0)
      		goto get_out;
	while (written < map.size && rret >= 0) {
    		rret = g_socket_send (sink->socket, (gchar *) map.data + written,
        		map.size - written, sink->cancellable, &err);
    		if (rret < 0)
		{
			GST_ERROR_OBJECT(sink,"Socket send error:%s\n",err->message) ;
		}
		else 
    			written += rret;
		if (err != NULL) g_error_free(err) ;
	}
  }

get_out:
  {
  GST_LOG_OBJECT (sink, "written %" G_GSIZE_FORMAT " bytes for buffer data, ret=%d", written,rret);

  gst_buffer_unmap (buf, &map);
  if (rret < 0)
      goto write_error;
  

  sink->data_written += written;

  GST_LOG_OBJECT (sink, "written in total %" G_GSIZE_FORMAT " bytes for buffer data", sink->data_written);
  return GST_FLOW_OK;
  }

  /* ERRORS */
write_error:
  {
    GstFlowReturn ret;

    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      ret = GST_FLOW_FLUSHING;
      GST_DEBUG_OBJECT (sink, "Cancelled reading from socket");
    } else {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
          (("Error while sending data to \"%s:%d\"."), sink->host, sink->port),
          ("Only %" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT " bytes written: %s",
              written, map.size, err->message));
	GST_DEBUG_OBJECT( sink, "Error while sending data to %s:%d err=%s\n", 
		sink->host, sink->port,err->message);
      ret = GST_FLOW_ERROR;
    }
    gst_buffer_unmap (buf, &map);
    g_clear_error (&err);
    return ret;
  }
}

static void
gst_r3p_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstR3PSink *r3psink;

  g_return_if_fail (GST_IS_R3P_SINK (object));
  r3psink = GST_R3P_SINK (object);

  switch (prop_id) {
    case PROP_HOST:
      if (!g_value_get_string (value)) {
        g_warning ("host property cannot be NULL");
        break;
      }
      g_free (r3psink->host);
      r3psink->host = g_strdup (g_value_get_string (value));
      GST_DEBUG_OBJECT(object, "Setting host value %s", g_value_get_string(value)) ;
      break;
    case PROP_PORT:
      r3psink->port = g_value_get_int (value);
      GST_DEBUG_OBJECT(object, "Setting port value %d", g_value_get_int(value)) ;
      break;
    case PROP_TIMEOUT:
      r3psink->timeout = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_r3p_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstR3PSink *r3psink;

  g_return_if_fail (GST_IS_R3P_SINK (object));
  r3psink = GST_R3P_SINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, r3psink->host);
      break;
    case PROP_PORT:
      g_value_set_int (value, r3psink->port);
      break;
    case PROP_TIMEOUT:
      g_value_set_int(value,r3psink->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_r3p_sink_start (GstBaseSink * bsink)
{
  GstR3PSink *thisobj = GST_R3P_SINK (bsink);
  GError *err = NULL;
  GInetAddress *addr;
  GSocketAddress *saddr;
  GResolver *resolver;

  GST_FIXME_OBJECT(GST_OBJECT(bsink),"Trying to start R3P Sink object:host %s port=%d\n",thisobj->host,thisobj->port) ;
  if (GST_OBJECT_FLAG_IS_SET (thisobj, GST_R3P_SINK_OPEN))
    return TRUE;

  /* look up name if we need to */
  addr = g_inet_address_new_from_string (thisobj->host);
  if (!addr) {
    GList *results;

    resolver = g_resolver_get_default ();

    results =
        g_resolver_lookup_by_name (resolver, thisobj->host, thisobj->cancellable,
        &err);
    if (!results)
      goto name_resolve;
    addr = G_INET_ADDRESS (g_object_ref (results->data));

    g_resolver_free_addresses (results);
    g_object_unref (resolver);
  }
#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *ip = g_inet_address_to_string (addr);

    GST_DEBUG_OBJECT (thisobj, "IP address for host %s is %s", thisobj->host, ip);
    g_free (ip);
  }
#endif
  saddr = g_inet_socket_address_new (addr, thisobj->port);
  g_object_unref (addr);

  /* create sending  socket */
  GST_DEBUG_OBJECT (thisobj, "opening sending  socket to %s:%d", thisobj->host,
      thisobj->port);
  thisobj->socket =
      g_socket_new (g_socket_address_get_family (saddr), G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_TCP, &err);
  g_socket_set_blocking(thisobj->socket,true) ;
  if (!thisobj->socket)
    goto no_socket;

  GST_DEBUG_OBJECT (thisobj, "opened sending  socket");

  /* connect to server */
  if (!g_socket_connect (thisobj->socket, saddr, thisobj->cancellable, &err))
    goto connect_failed;

  g_object_unref (saddr);

  GST_OBJECT_FLAG_SET (thisobj, GST_R3P_SINK_OPEN);

  thisobj->data_written = 0;

  return TRUE;
no_socket:
  {
    GST_ELEMENT_ERROR (thisobj, RESOURCE, OPEN_READ, (NULL),
        ("Failed to create socket: %s", err->message));
    g_clear_error (&err);
    g_object_unref (saddr);
    return FALSE;
  }
name_resolve:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (thisobj, "Cancelled name resolval");
    } else {
      GST_ELEMENT_ERROR (thisobj, RESOURCE, OPEN_READ, (NULL),
          ("Failed to resolve host '%s': %s", thisobj->host, err->message));
    }
    g_clear_error (&err);
    g_object_unref (resolver);
    return FALSE;
  }
connect_failed:
  {
    GST_DEBUG_OBJECT (thisobj, "Connecting failure!!");
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (thisobj, "Cancelled connecting");
    } else {
      GST_ELEMENT_ERROR (thisobj, RESOURCE, OPEN_READ, (NULL),
          ("Failed to connect to host '%s:%d': %s", thisobj->host, thisobj->port,
              err->message));
    }
    g_clear_error (&err);
    g_object_unref (saddr);
    /* pretend we opened ok for proper cleanup to happen */
    GST_OBJECT_FLAG_SET (thisobj, GST_R3P_SINK_OPEN);
    gst_r3p_sink_stop (GST_BASE_SINK (thisobj));
    return FALSE;
  }
}

static gboolean
gst_r3p_sink_stop (GstBaseSink * bsink)
{
  GstR3PSink *thisobj = GST_R3P_SINK (bsink);
  GError *err = NULL;

  if (!GST_OBJECT_FLAG_IS_SET (thisobj, GST_R3P_SINK_OPEN))
    return TRUE;

  if (thisobj->socket) {
    GST_DEBUG_OBJECT (thisobj, "closing socket");

    if (!g_socket_close (thisobj->socket, &err)) {
      GST_ERROR_OBJECT (thisobj, "Failed to close socket: %s", err->message);
      g_clear_error (&err);
    }
    g_object_unref (thisobj->socket);
    thisobj->socket = NULL;
  }

  GST_OBJECT_FLAG_UNSET (thisobj, GST_R3P_SINK_OPEN);

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_r3p_sink_unlock (GstBaseSink * bsink)
{
  GstR3PSink *sink = GST_R3P_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "set to flushing");
  g_cancellable_cancel (sink->cancellable);

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_r3p_sink_unlock_stop (GstBaseSink * bsink)
{
  GstR3PSink *sink = GST_R3P_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "unset flushing");
  g_object_unref (sink->cancellable);
  sink->cancellable = g_cancellable_new ();

  return TRUE;
}

