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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-unixclientsrc
 * @see_also: #unixclientsink
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * # server:
 * gst-launch fdsink fd=1 ! unixserversink path=/tmp/unix.sock
 * # client:
 * gst-launch unixclientsrc path=/tmp/unix.sock ! fdsink fd=2
 * ]| everything you type in the server is shown on the client
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>
#include "gstunixclientsrc.h"
#include "gsttcp.h"

GST_DEBUG_CATEGORY_STATIC (unixclientsrc_debug);
#define GST_CAT_DEFAULT unixclientsrc_debug

#define MAX_READ_SIZE                   4 * 1024


static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


enum
{
  PROP_0,
  PROP_PATH
};

#define gst_unix_client_src_parent_class parent_class
G_DEFINE_TYPE (GstUNIXClientSrc, gst_unix_client_src, GST_TYPE_PUSH_SRC);


static void gst_unix_client_src_finalize (GObject * gobject);

static GstCaps *gst_unix_client_src_getcaps (GstBaseSrc * psrc,
    GstCaps * filter);

static GstFlowReturn gst_unix_client_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_unix_client_src_stop (GstBaseSrc * bsrc);
static gboolean gst_unix_client_src_start (GstBaseSrc * bsrc);
static gboolean gst_unix_client_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_unix_client_src_unlock_stop (GstBaseSrc * bsrc);

static void gst_unix_client_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_unix_client_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_unix_client_src_class_init (GstUNIXClientSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_unix_client_src_set_property;
  gobject_class->get_property = gst_unix_client_src_get_property;
  gobject_class->finalize = gst_unix_client_src_finalize;

  g_object_class_install_property (gobject_class, PROP_PATH,
      g_param_spec_string ("path", "path", "The UNIX socket path to open",
          UNIX_DEFAULT_PATH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "UNIX client source", "Source/Local",
      "Receive data as a client via a UNIX socket",
      "Stefan Junker <code at stefanjunker dot de>");

  gstbasesrc_class->get_caps = gst_unix_client_src_getcaps;
  gstbasesrc_class->start = gst_unix_client_src_start;
  gstbasesrc_class->stop = gst_unix_client_src_stop;
  gstbasesrc_class->unlock = gst_unix_client_src_unlock;
  gstbasesrc_class->unlock_stop = gst_unix_client_src_unlock_stop;

  gstpush_src_class->create = gst_unix_client_src_create;

  GST_DEBUG_CATEGORY_INIT (unixclientsrc_debug, "unixclientsrc", 0,
      "UNIX Client Source");
}

static void
gst_unix_client_src_init (GstUNIXClientSrc * this)
{
  this->path = g_strdup (UNIX_DEFAULT_PATH);
  this->socket = NULL;
  this->cancellable = g_cancellable_new ();

  GST_OBJECT_FLAG_UNSET (this, GST_UNIX_CLIENT_SRC_OPEN);
}

static void
gst_unix_client_src_finalize (GObject * gobject)
{
  GstUNIXClientSrc *this = GST_UNIX_CLIENT_SRC (gobject);

  if (this->cancellable)
    g_object_unref (this->cancellable);
  this->cancellable = NULL;
  if (this->socket)
    g_object_unref (this->socket);
  this->socket = NULL;
  g_free (this->path);
  this->path = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstCaps *
gst_unix_client_src_getcaps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstUNIXClientSrc *src;
  GstCaps *caps = NULL;

  src = GST_UNIX_CLIENT_SRC (bsrc);

  caps = (filter ? gst_caps_ref (filter) : gst_caps_new_any ());

  GST_DEBUG_OBJECT (src, "returning caps %" GST_PTR_FORMAT, caps);
  g_assert (GST_IS_CAPS (caps));
  return caps;
}

static GstFlowReturn
gst_unix_client_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstUNIXClientSrc *src;
  GstFlowReturn ret = GST_FLOW_OK;
  gssize rret;
  GError *err = NULL;
  GstMapInfo map;
  gssize avail, read;

  src = GST_UNIX_CLIENT_SRC (psrc);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_UNIX_CLIENT_SRC_OPEN))
    goto wrong_state;

  GST_LOG_OBJECT (src, "asked for a buffer");

  /* read the buffer header */
  avail = g_socket_get_available_bytes (src->socket);
  if (avail < 0) {
    goto get_available_error;
  } else if (avail == 0) {
    GIOCondition condition;

    if (!g_socket_condition_wait (src->socket,
            G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, src->cancellable, &err))
      goto select_error;

    condition =
        g_socket_condition_check (src->socket,
        G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP);

    if ((condition & G_IO_ERR)) {
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Socket in error state"));
      *outbuf = NULL;
      ret = GST_FLOW_ERROR;
      goto done;
    } else if ((condition & G_IO_HUP)) {
      GST_DEBUG_OBJECT (src, "Connection closed");
      *outbuf = NULL;
      ret = GST_FLOW_EOS;
      goto done;
    }
    avail = g_socket_get_available_bytes (src->socket);
    if (avail < 0)
      goto get_available_error;
  }

  if (avail > 0) {
    read = MIN (avail, MAX_READ_SIZE);
    *outbuf = gst_buffer_new_and_alloc (read);
    gst_buffer_map (*outbuf, &map, GST_MAP_READWRITE);
    rret =
        g_socket_receive (src->socket, (gchar *) map.data, read,
        src->cancellable, &err);
  } else {
    /* Connection closed */
    *outbuf = NULL;
    read = 0;
    rret = 0;
  }

  if (rret == 0) {
    GST_DEBUG_OBJECT (src, "Connection closed");
    ret = GST_FLOW_EOS;
    if (*outbuf) {
      gst_buffer_unmap (*outbuf, &map);
      gst_buffer_unref (*outbuf);
    }
    *outbuf = NULL;
  } else if (rret < 0) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      ret = GST_FLOW_FLUSHING;
      GST_DEBUG_OBJECT (src, "Cancelled reading from socket");
    } else {
      ret = GST_FLOW_ERROR;
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Failed to read from socket: %s", err->message));
    }
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  } else {
    ret = GST_FLOW_OK;
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_resize (*outbuf, 0, rret);

    GST_LOG_OBJECT (src,
        "Returning buffer from _get of size %" G_GSIZE_FORMAT ", ts %"
        GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
        ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
        gst_buffer_get_size (*outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (*outbuf)),
        GST_BUFFER_OFFSET (*outbuf), GST_BUFFER_OFFSET_END (*outbuf));
  }
  g_clear_error (&err);

done:
  return ret;

select_error:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (src, "Cancelled");
      ret = GST_FLOW_FLUSHING;
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Select failed: %s", err->message));
      ret = GST_FLOW_ERROR;
    }
    g_clear_error (&err);
    return ret;
  }
get_available_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Failed to get available bytes from socket"));
    return GST_FLOW_ERROR;
  }
wrong_state:
  {
    GST_DEBUG_OBJECT (src, "connection to closed, cannot read data");
    return GST_FLOW_FLUSHING;
  }
}

static void
gst_unix_client_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstUNIXClientSrc *unixclientsrc = GST_UNIX_CLIENT_SRC (object);

  switch (prop_id) {
    case PROP_PATH:
      if (!g_value_get_string (value)) {
        g_warning ("path property cannot be NULL");
        break;
      }
      g_free (unixclientsrc->path);
      unixclientsrc->path = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_unix_client_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstUNIXClientSrc *unixclientsrc = GST_UNIX_CLIENT_SRC (object);

  switch (prop_id) {
    case PROP_PATH:
      g_value_set_string (value, unixclientsrc->path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* create a socket for connecting to remote server */
static gboolean
gst_unix_client_src_start (GstBaseSrc * bsrc)
{
  GstUNIXClientSrc *src = GST_UNIX_CLIENT_SRC (bsrc);
  GError *err = NULL;
  GSocketAddress *usaddr;

  usaddr = g_unix_socket_address_new (src->path);

  /* create receiving client socket */
  GST_DEBUG_OBJECT (src, "opening receiving client socket at %s",
      src->path);

  src->socket =
      g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (!src->socket)
    goto no_socket;

  GST_DEBUG_OBJECT (src, "opened receiving client socket at %s", src->path);
  GST_OBJECT_FLAG_SET (src, GST_UNIX_CLIENT_SRC_OPEN);

  /* connect to server */
  if (!g_socket_connect (src->socket, usaddr, src->cancellable, &err))
    goto connect_failed;
  GST_DEBUG_OBJECT (src, "connected to socket at %s", src->path);

  g_object_unref (usaddr);

  return TRUE;

no_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Failed to create socket: %s", err->message));
    g_clear_error (&err);
    g_object_unref (usaddr);
    return FALSE;
  }
connect_failed:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (src, "Cancelled connecting");
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Failed to connect to socket at '%s': %s", src->path, err->message));
    }
    g_clear_error (&err);
    g_object_unref (usaddr);
    gst_unix_client_src_stop (GST_BASE_SRC (src));
    return FALSE;
  }
}

/* close the socket and associated resources
 * unset OPEN flag
 * used both to recover from errors and go to NULL state */
static gboolean
gst_unix_client_src_stop (GstBaseSrc * bsrc)
{
  GstUNIXClientSrc *src;
  GError *err = NULL;

  src = GST_UNIX_CLIENT_SRC (bsrc);

  if (src->socket) {
    GST_DEBUG_OBJECT (src, "closing socket");

    if (!g_socket_close (src->socket, &err)) {
      GST_ERROR_OBJECT (src, "Failed to close socket: %s", err->message);
      g_clear_error (&err);
    }
    g_object_unref (src->socket);
    src->socket = NULL;
  }

  GST_OBJECT_FLAG_UNSET (src, GST_UNIX_CLIENT_SRC_OPEN);

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_unix_client_src_unlock (GstBaseSrc * bsrc)
{
  GstUNIXClientSrc *src = GST_UNIX_CLIENT_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "set to flushing");
  g_cancellable_cancel (src->cancellable);

  return TRUE;
}

/* will be called only between calls to start() and stop() */
static gboolean
gst_unix_client_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstUNIXClientSrc *src = GST_UNIX_CLIENT_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "unset flushing");
  g_cancellable_reset (src->cancellable);

  return TRUE;
}
