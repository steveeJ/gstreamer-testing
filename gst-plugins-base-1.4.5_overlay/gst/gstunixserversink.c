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

/**
 * SECTION:element-unixserversink
 * @see_also: #multifdsink
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * # server:
 * gst-launch fdsrc fd=1 ! unixserversink path=/tmp/unix.sock
 * # client:
 * gst-launch unixclientsrc path=/tmp/unix.sock ! fdsink fd=2
 * ]| 
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst-i18n-plugin.h>
#include <string.h>             /* memset */
#include <sys/stat.h>

#include "gstunixserversink.h"

#define UNIX_BACKLOG             5

GST_DEBUG_CATEGORY_STATIC (unixserversink_debug);
#define GST_CAT_DEFAULT (unixserversink_debug)

enum
{
  PROP_0,
  PROP_PATH,
};

static void gst_unix_server_sink_finalize (GObject * gobject);

static gboolean gst_unix_server_sink_init_send (GstMultiHandleSink * this);
static gboolean gst_unix_server_sink_close (GstMultiHandleSink * this);
static void gst_unix_server_sink_removed (GstMultiHandleSink * sink,
    GstMultiSinkHandle handle);

static void gst_unix_server_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_unix_server_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_unix_server_sink_parent_class parent_class
G_DEFINE_TYPE (GstUNIXServerSink, gst_unix_server_sink,
    GST_TYPE_MULTI_SOCKET_SINK);

static void
gst_unix_server_sink_class_init (GstUNIXServerSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstMultiHandleSinkClass *gstmultihandlesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstmultihandlesink_class = (GstMultiHandleSinkClass *) klass;

  gobject_class->set_property = gst_unix_server_sink_set_property;
  gobject_class->get_property = gst_unix_server_sink_get_property;
  gobject_class->finalize = gst_unix_server_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_PATH,
      g_param_spec_string ("path", "path", "The UNIX socket path to listen on",
          UNIX_DEFAULT_PATH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "UNIX server sink", "Sink/Local",
      "Send data as a server via a UNIX socket",
      "Stefan Junker <code at stefanjunker dot de>");

  gstmultihandlesink_class->init = gst_unix_server_sink_init_send;
  gstmultihandlesink_class->close = gst_unix_server_sink_close;
  gstmultihandlesink_class->removed = gst_unix_server_sink_removed;

  GST_DEBUG_CATEGORY_INIT (unixserversink_debug, "unixserversink", 0, "UNIX sink");
}

static void
gst_unix_server_sink_init (GstUNIXServerSink * this)
{
  this->path = g_strdup (UNIX_DEFAULT_PATH);
  this->server_socket = NULL;
}

static void
gst_unix_server_sink_finalize (GObject * gobject)
{
  GstUNIXServerSink *this = GST_UNIX_SERVER_SINK (gobject);

  GST_DEBUG_OBJECT (this, "finalizing");

  if (this->server_socket)
    g_object_unref (this->server_socket);
  this->server_socket = NULL;

  if (this->path) {
    struct stat statbuf;
    stat(this->path, &statbuf);
    int path_is_socket = S_ISSOCK(statbuf.st_mode);
    GST_DEBUG ("path %s is socket: %i", this->path, path_is_socket);
    if (path_is_socket && !remove(this->path)) {
      GST_ERROR ("Could not remove socket file");
    }

    g_free (this->path);
    this->path = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

/* handle a read request on the server,
 * which indicates a new client connection */
static gboolean
gst_unix_server_sink_handle_server_read (GstUNIXServerSink * sink)
{
  GstMultiSinkHandle handle;
  GSocket *client_socket;
  GError *err = NULL;

  /* wait on server socket for connections */
  client_socket =
      g_socket_accept (sink->server_socket, sink->element.cancellable, &err);
  if (!client_socket)
    goto accept_failed;

  handle.socket = client_socket;
  /* gst_multi_handle_sink_add does not take ownership of client_socket */
  gst_multi_handle_sink_add (GST_MULTI_HANDLE_SINK (sink), handle);

#ifndef GST_DISABLE_GST_DEBUG
  {
    GST_DEBUG_OBJECT (sink, "Received new client. %p", client_socket);
  }
#endif

  g_object_unref (client_socket);
  return TRUE;

  /* ERRORS */
accept_failed:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Could not accept client on server socket %p: %s",
            sink->server_socket, err->message));
    g_clear_error (&err);
    return FALSE;
  }
}

static void
gst_unix_server_sink_removed (GstMultiHandleSink * sink,
    GstMultiSinkHandle handle)
{
  GError *err = NULL;

  GST_DEBUG_OBJECT (sink, "closing socket");

  if (!g_socket_close (handle.socket, &err)) {
    GST_ERROR_OBJECT (sink, "Failed to close socket: %s", err->message);
    g_clear_error (&err);
  }
}

static gboolean
gst_unix_server_sink_socket_condition (GSocket * socket, GIOCondition condition,
    GstUNIXServerSink * sink)
{

  if ((condition & G_IO_ERR)) {
    GST_DEBUG_OBJECT (sink, "Incoming socket condition ERR");
    goto error;
  } else if ((condition & G_IO_IN) || (condition & G_IO_PRI)) {
    GST_DEBUG_OBJECT (sink, "Incoming socket condition IN or PRI");
    if (!gst_unix_server_sink_handle_server_read (sink))
      return FALSE;
  } else {
     GST_DEBUG_OBJECT (sink, "Incoming unknown socket condition: %i", 
             condition);
  }

  return TRUE;

error:
  GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
      ("client connection failed"));

  return FALSE;
}

static void
gst_unix_server_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstUNIXServerSink *sink;

  sink = GST_UNIX_SERVER_SINK (object);

  GST_DEBUG_OBJECT (sink, "setting property: %i=%s", prop_id, value);

  switch (prop_id) {
    case PROP_PATH:
      if (!g_value_get_string (value)) {
        g_warning ("path property cannot be NULL");
        break;
      }
      g_free (sink->path);
      sink->path = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_unix_server_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstUNIXServerSink *sink;

  sink = GST_UNIX_SERVER_SINK (object);

  GST_DEBUG_OBJECT (sink, "getting property: %i", prop_id);

  switch (prop_id) {
    case PROP_PATH:
      g_value_set_string (value, sink->path);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_unix_server_sink_init_send (GstMultiHandleSink * parent)
{
  GstUNIXServerSink *this = GST_UNIX_SERVER_SINK (parent);
  GError *err = NULL;
  GUnixSocketAddress *usaddr;
//  GInetAddress *addr;
//  GResolver *resolver;
//  gint bound_port;

//#ifndef GST_DISABLE_GST_DEBUG
//  {
    GST_DEBUG_OBJECT (this, "Trying to create UNIX socket at %s", this->path);
//  }
//#endif
  usaddr = g_unix_socket_address_new (this->path);

  /* create the server listener socket */
  this->server_socket =
      g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (!this->path)
    goto no_socket;

  GST_DEBUG_OBJECT (this, "Created UNIX socket at %s", this->path);

  g_socket_set_blocking (this->server_socket, FALSE);

  /* bind it */
  GST_DEBUG_OBJECT (this, "binding server socket to address");
  if (!g_socket_bind (this->server_socket, usaddr, TRUE, &err))
    goto bind_failed;

  g_object_unref (usaddr);

  GST_DEBUG_OBJECT (this, "listening on server socket");
  g_socket_set_listen_backlog (this->server_socket, UNIX_BACKLOG);

  if (!g_socket_listen (this->server_socket, &err))
    goto listen_failed;

  GST_DEBUG_OBJECT (this, "listened on server socket %p", this->server_socket);

  this->server_source =
      g_socket_create_source (this->server_socket,
      G_IO_IN | G_IO_OUT | G_IO_PRI | G_IO_ERR | G_IO_HUP,
      this->element.cancellable);
  g_source_set_callback (this->server_source,
      (GSourceFunc) gst_unix_server_sink_socket_condition, gst_object_ref (this),
      (GDestroyNotify) gst_object_unref);
  g_source_attach (this->server_source, this->element.main_context);

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Failed to create socket: %s", err->message));
    g_clear_error (&err);
    g_object_unref (usaddr);
    return FALSE;
  }
bind_failed:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (this, "Cancelled binding");
    } else {
      GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
          ("Failed to bind on path '%s': %s", this->path, err->message));
    }
    g_clear_error (&err);
    g_object_unref (usaddr);
    gst_unix_server_sink_close (GST_MULTI_HANDLE_SINK (&this->element));
    return FALSE;
  }
listen_failed:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (this, "Cancelled listening");
    } else {
      GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
          ("Failed to listen on path '%s': %s", this->path, err->message));
    }
    g_clear_error (&err);
    gst_unix_server_sink_close (GST_MULTI_HANDLE_SINK (&this->element));
    return FALSE;
  }
}

static gboolean
gst_unix_server_sink_close (GstMultiHandleSink * parent)
{
  GstUNIXServerSink *this = GST_UNIX_SERVER_SINK (parent);

  if (this->server_source) {
    GST_DEBUG_OBJECT (this, "destroying server_source");
    g_source_destroy (this->server_source);
    GST_DEBUG_OBJECT (this, "unref server_source");
    g_source_unref (this->server_source);
    this->server_source = NULL;
  }

  if (this->server_socket) {
    GError *err = NULL;

    GST_DEBUG_OBJECT (this, "closing socket");

    if (!g_socket_close (this->server_socket, &err)) {
      GST_ERROR_OBJECT (this, "Failed to close socket: %s", err->message);
      g_clear_error (&err);
    }
    g_object_unref (this->server_socket);
    this->server_socket = NULL;
  }

  return TRUE;
}
