/*
 * GStreamer
 * Copyright (C) 2010 Jan Schmidt <thaytan@noraisin.net>
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
 * SECTION:element-rtmpsink
 *
 * This element delivers data to a streaming server via RTMP. It uses
 * librtmp, and supports any protocols/urls that librtmp supports.
 * The URL/location can contain extra connection or session parameters
 * for librtmp, such as 'flashver=version'. See the librtmp documentation
 * for more detail
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! ffenc_flv ! flvmux ! rtmpsink location='rtmp://localhost/path/to/stream live=1'
 * ]| Encode a test video stream to FLV video format and stream it via RTMP.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <gst/gst.h>

#include "gstrtmpsink.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtmp_sink_debug);
#define GST_CAT_DEFAULT gst_rtmp_sink_debug

#define MAX_TCP_TIMEOUT 3000000000LL

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_RECONNECTION_DELAY,
  PROP_TCP_TIMEOUT,
  ARG_LOG_LEVEL
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-flv")
    );

static void gst_rtmp_sink_finalize (GstRTMPSink * sink);
static void gst_rtmp_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_rtmp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtmp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_rtmp_sink_stop (GstBaseSink * sink);
static gboolean gst_rtmp_sink_start (GstBaseSink * sink);
static GstFlowReturn gst_rtmp_sink_render (GstBaseSink * sink, GstBuffer * buf);

static void
_do_init (GType gtype)
{
  static const GInterfaceInfo urihandler_info = {
    gst_rtmp_sink_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (gtype, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_rtmp_sink_debug, "rtmpsink", 0,
      "RTMP server element");
}

GST_BOILERPLATE_FULL (GstRTMPSink, gst_rtmp_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);


static void
gst_rtmp_sink_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "RTMP output sink",
      "Sink/Network", "Sends FLV content to a server via RTMP",
      "Jan Schmidt <thaytan@noraisin.net>, Anthony Violo <anthony.violo@ubicast.eu>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

}

/* initialize the plugin's class */
static void
gst_rtmp_sink_class_init (GstRTMPSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_rtmp_sink_set_property;
  gobject_class->get_property = gst_rtmp_sink_get_property;

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_rtmp_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_rtmp_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_rtmp_sink_render);
  gobject_class->finalize = (GObjectFinalizeFunc)gst_rtmp_sink_finalize;

  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", PROP_LOCATION, G_PARAM_READWRITE, NULL);

  g_object_class_install_property (gobject_class, ARG_LOG_LEVEL,
    g_param_spec_int ("log-level", "Log level",
        "librtmp log level", RTMP_LOGCRIT, RTMP_LOGALL, RTMP_LOGERROR,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_RECONNECTION_DELAY, g_param_spec_uint64 ("reconnection-delay",
      "Delay between each reconnection in ns. 0 means that an error occurs when disconnected",
      "Delay between each reconnection in ns. 0 means that an error occurs when disconnected",
      0, G_MAXINT64, 10000000000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TCP_TIMEOUT,  g_param_spec_uint64 ("tcp-timeout",
      "Custom TCP timeout in ns. If 0, socket is in blocking mode (default librtmp behaviour)",
      "Custom TCP timeout in ns. If 0, socket is in blocking mode (default librtmp behaviour)",
      0, MAX_TCP_TIMEOUT, MAX_TCP_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_rtmp_sink_finalize (GstRTMPSink * sink)
{
  GST_DEBUG_OBJECT (sink, "free all variables stored in memory");
  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (sink));
}

static void
gst_rtmp_sink_init (GstRTMPSink * sink, GstRTMPSinkClass * klass)
{
  sink->connection_status = 0;
  sink->reconnection_delay = 10000000000;
  sink->tcp_timeout = MAX_TCP_TIMEOUT;
  sink->stream_meta_saved = FALSE;
  sink->video_meta_saved = FALSE;
  sink->audio_meta_saved = FALSE;
  sink->try_now_connection = TRUE;
  sink->send_error_count = 0;
  sink->disconnection_notified = 1;
}

static gboolean
gst_rtmp_sink_start (GstBaseSink * basesink)
{
  GstRTMPSink *sink = GST_RTMP_SINK (basesink);

  if (!sink->uri) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        ("Please set URI for RTMP output"), ("No URI set before starting"));
    return FALSE;
  }

  sink->rtmp_uri = g_strdup (sink->uri);
  sink->rtmp = RTMP_Alloc ();
  RTMP_Init (sink->rtmp);
  //sink->rtmp->m_sb.asynchronous = 1;
  
  if (!RTMP_SetupURL (sink->rtmp, sink->rtmp_uri)) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Failed to setup URL '%s'", sink->uri));
    RTMP_Free (sink->rtmp);
    sink->rtmp = NULL;
    g_free (sink->rtmp_uri);
    sink->rtmp_uri = NULL;
    return FALSE;
  }
  GST_DEBUG_OBJECT (sink, "Created RTMP object");
  /* Mark this as an output connection */
  RTMP_EnableWrite (sink->rtmp);

  sink->reconnection_required = TRUE;
  return TRUE;
}

static gboolean
gst_rtmp_sink_stop (GstBaseSink * basesink)
{
  GstRTMPSink *sink = GST_RTMP_SINK (basesink);

  gst_buffer_replace (&sink->cache, NULL);
  if (sink->rtmp) {
    RTMP_Close (sink->rtmp);
    RTMP_Free (sink->rtmp);
    sink->rtmp = NULL;
  }
  if (sink->rtmp_uri) {
    g_free (sink->rtmp_uri);
    sink->rtmp_uri = NULL;
  }
  return TRUE;
}

static
gboolean    copy_metadata(GstBuffer **meta_buf, GstBuffer *buf)
{
    *meta_buf = gst_buffer_new_and_alloc(GST_BUFFER_SIZE (buf));
    *meta_buf = gst_buffer_copy(buf);
    return TRUE;
}

static GstFlowReturn
gst_rtmp_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstRTMPSink *sink = GST_RTMP_SINK (bsink);
  GstBuffer *reffed_buf = NULL;
  GstStructure *s;

  if (sink->connection_status) {
    if (!sink->stream_meta_saved && buf->data[0] == 18) {
        GST_LOG_OBJECT (sink, "save stream metadata, size : %d", GST_BUFFER_SIZE (buf));
        sink->stream_meta_saved = copy_metadata(&sink->stream_metadata, buf);
    }
    // Check if first packet is video type (video = 9) contain video type
    else if (!sink->video_meta_saved && buf->data[0] == 9) {
        GST_LOG_OBJECT (sink, "save video metada, size : %d", GST_BUFFER_SIZE (buf));
        sink->video_meta_saved = copy_metadata(&sink->video_metadata, buf);
    }
    // Check if first packet is audio type (audio = 8) contain video type
    else if (!sink->audio_meta_saved && buf->data[0] == 8) {
        GST_LOG_OBJECT (sink, "save audio metada, size : %d", GST_BUFFER_SIZE (buf));
        sink->audio_meta_saved = copy_metadata(&sink->audio_metadata, buf);
    }
  }

  if (sink->reconnection_required) {
    if ((sink->sent_status == -1 || sink->connection_status == -1))
      sink->end_time_disc = GST_BUFFER_TIMESTAMP (buf);
    if ((sink->end_time_disc - sink->begin_time_disc > sink->reconnection_delay) || sink->try_now_connection) {
      GST_DEBUG_OBJECT (sink, "Maybe disconnected from RTMP server, reconnecting to be sure");
      if (sink->connection_status == -1 || sink->sent_status == -1) {
        GST_DEBUG_OBJECT (sink, "Reinitializing RTMP object");
        gst_rtmp_sink_stop (bsink);
        gst_rtmp_sink_start (bsink);
        sink->begin_time_disc = sink->end_time_disc;
      }
      if (!RTMP_IsConnected (sink->rtmp)) {
        GST_DEBUG_OBJECT (sink, "Trying to connect");
        //if (!RTMP_Connect (sink->rtmp, NULL, sink->tcp_timeout)
            //|| !RTMP_ConnectStream (sink->rtmp, 0)) {
        if (!RTMP_Connect (sink->rtmp, NULL)
            || !RTMP_ConnectStream (sink->rtmp, 0)) {
          GST_DEBUG_OBJECT (sink, "Connection failed, freeing RTMP buffers");
          RTMP_Free (sink->rtmp);
          sink->rtmp = NULL;
          g_free (sink->rtmp_uri);
          sink->try_now_connection = FALSE;
          sink->rtmp_uri = NULL;
          sink->connection_status = -1;
          sink->send_error_count = 0;
          if (sink->reconnection_delay <= 0)
            return GST_FLOW_ERROR;
          else {
            sink->begin_time_disc = GST_BUFFER_TIMESTAMP (buf);
            if (sink->disconnection_notified == 1) {
                GST_DEBUG_OBJECT (sink, "Emitting disconnected message");
                s = gst_structure_new ("disconnected",
                    "timestamp", G_TYPE_UINT64, sink->begin_time_disc, NULL);
                gst_element_post_message (GST_ELEMENT (sink),
                    gst_message_new_element (GST_OBJECT (sink), s));
                sink->connection_status = -1;
                sink->sent_status = 0;
                sink->disconnection_notified = 0;
            }
            return GST_FLOW_OK;
          }
        }
        GST_DEBUG_OBJECT (sink, "Opened connection to %s", sink->rtmp_uri);
      }
      /* FIXME: Parse the first buffer and see if it contains a header plus a packet instead
      * of just assuming it's only the header */
      GST_LOG_OBJECT (sink, "Caching first buffer of size %d for concatenation",
          GST_BUFFER_SIZE (buf));
      gst_buffer_replace (&sink->cache, buf);   
      sink->reconnection_required = FALSE;
      if (!sink->disconnection_notified) {
        GST_DEBUG_OBJECT (sink, "Success to reconnect to server, emitting reconnected message");
        s = gst_structure_new ("reconnected",
          "timestamp", G_TYPE_UINT64, sink->begin_time_disc, NULL);
          gst_element_post_message (GST_ELEMENT (sink),
        gst_message_new_element (GST_OBJECT (sink), s));
        sink->disconnection_notified = 1;
      }
      else if (sink->sent_status == -1 && sink->send_error_count >= 2) {
        GST_DEBUG_OBJECT (sink, "Insufficient bandwidth", sink->uri);
        s = gst_structure_new ("bandwidth",
            "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (buf), NULL);
        gst_element_post_message (GST_ELEMENT (sink),
            gst_message_new_element (GST_OBJECT (sink), s));
        sink->send_error_count = 0;
       }
       sink->connection_status = 1;
       GST_DEBUG_OBJECT (sink, "Send back stream metadata to the server, dropping video/audio buffer");
       if (sink->stream_meta_saved)
         sink->connection_status = RTMP_Write (sink->rtmp,
           (char *) GST_BUFFER_DATA (sink->stream_metadata), GST_BUFFER_SIZE (sink->stream_metadata));
       if (sink->video_meta_saved)
         sink->connection_status = RTMP_Write (sink->rtmp,
           (char *) GST_BUFFER_DATA (sink->video_metadata), GST_BUFFER_SIZE (sink->video_metadata));
       if (sink->audio_meta_saved)
         sink->connection_status = RTMP_Write (sink->rtmp,
           (char *) GST_BUFFER_DATA (sink->audio_metadata), GST_BUFFER_SIZE (sink->audio_metadata));
       return GST_FLOW_OK;
    }
    else
      return GST_FLOW_OK;
  }
  if (sink->cache) {
    GST_LOG_OBJECT (sink, "Joining 2nd buffer of size %d to cached buf",
        GST_BUFFER_SIZE (buf));
    gst_buffer_ref (buf);
    reffed_buf = buf = gst_buffer_join (sink->cache, buf);
    sink->cache = NULL;
  }
  if (sink->connection_status > 0) {
    GST_LOG_OBJECT (sink, "Sending %d bytes to RTMP server",
        GST_BUFFER_SIZE (buf)); 
      if (!(sink->sent_status = RTMP_Write (sink->rtmp,
                  (char *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf)))) {
        GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
            ("Allocation or flv packet too small error"));
        if (reffed_buf)
          gst_buffer_unref (reffed_buf);
        return GST_FLOW_ERROR;
      }
  }
  if (sink->sent_status == -1) {
    GST_DEBUG_OBJECT (sink, "RTMP send error");
    sink->send_error_count++;
    sink->reconnection_required = TRUE;
    sink->begin_time_disc = GST_BUFFER_TIMESTAMP (buf);
    sink->try_now_connection = TRUE;
  }
  if (reffed_buf)
    gst_buffer_unref (reffed_buf);
  return GST_FLOW_OK;
}

/*
 * URI interface support.
 */
static GstURIType
gst_rtmp_sink_uri_get_type (void)
{
  return GST_URI_SINK;
}

static gchar **
gst_rtmp_sink_uri_get_protocols (void)
{
  static gchar *protocols[] =
      { (char *) "rtmp", (char *) "rtmpt", (char *) "rtmps", (char *) "rtmpe",
    (char *) "rtmfp", (char *) "rtmpte", (char *) "rtmpts", NULL
  };
  return protocols;
}

static const gchar *
gst_rtmp_sink_uri_get_uri (GstURIHandler * handler)
{
  GstRTMPSink *sink = GST_RTMP_SINK (handler);

  return sink->uri;
}

static gboolean
gst_rtmp_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstRTMPSink *sink = GST_RTMP_SINK (handler);
  gboolean ret = TRUE;

  if (GST_STATE (sink) >= GST_STATE_PAUSED)
    return FALSE;

  g_free (sink->uri);
  sink->uri = NULL;

  if (uri != NULL) {
    int protocol;
    AVal host;
    unsigned int port;
    AVal playpath, app;

    if (!RTMP_ParseURL (uri, &protocol, &host, &port, &playpath, &app) ||
        !host.av_len || !playpath.av_len) {
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
          ("Failed to parse URI %s", uri), (NULL));
      ret = FALSE;
    } else {
      sink->uri = g_strdup (uri);
    }
    if (playpath.av_val)
      free (playpath.av_val);
  }
  if (ret)
    GST_DEBUG_OBJECT (sink, "Changed URI to %s", GST_STR_NULL (uri));
  return TRUE;
}

static void
gst_rtmp_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtmp_sink_uri_get_type;
  iface->get_protocols = gst_rtmp_sink_uri_get_protocols;
  iface->get_uri = gst_rtmp_sink_uri_get_uri;
  iface->set_uri = gst_rtmp_sink_uri_set_uri;
}

static void
gst_rtmp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTMPSink *sink = GST_RTMP_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_rtmp_sink_uri_set_uri (GST_URI_HANDLER (sink),
          g_value_get_string (value));
      break;
    case PROP_RECONNECTION_DELAY:
      sink->reconnection_delay = g_value_get_uint64 (value);
      break;
    case PROP_TCP_TIMEOUT:
      sink->tcp_timeout = g_value_get_uint64 (value);
      break;
    case ARG_LOG_LEVEL:
	  RTMP_debuglevel = g_value_get_int(value);
	  break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtmp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTMPSink *sink = GST_RTMP_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->uri);
      break;
    case PROP_RECONNECTION_DELAY:
      g_value_set_uint64 (value, sink->reconnection_delay);
      break;
    case PROP_TCP_TIMEOUT:
      g_value_set_uint64 (value, sink->tcp_timeout);
      break;
    case ARG_LOG_LEVEL:
      g_value_set_int(value, RTMP_debuglevel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
