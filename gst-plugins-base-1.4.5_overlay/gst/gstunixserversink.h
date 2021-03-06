/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2015> Stefan Junker <code at stefanjunker dot de>
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


#ifndef __GST_UNIX_SERVER_SINK_H__
#define __GST_UNIX_SERVER_SINK_H__


#include <gst/gst.h>
#include <gio/gio.h>
#include <gio-unix-2.0/gio/gunixsocketaddress.h>

G_BEGIN_DECLS

#include "gstmultisocketsink.h"

#define UNIX_DEFAULT_PATH "/tmp/gst-unix.sock"

#define GST_TYPE_UNIX_SERVER_SINK \
  (gst_unix_server_sink_get_type())
#define GST_UNIX_SERVER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_UNIX_SERVER_SINK,GstUNIXServerSink))
#define GST_UNIX_SERVER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_UNIX_SERVER_SINK,GstUNIXServerSinkClass))
#define GST_IS_UNIX_SERVER_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_UNIX_SERVER_SINK))
#define GST_IS_UNIX_SERVER_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_UNIX_SERVER_SINK))

typedef struct _GstUNIXServerSink GstUNIXServerSink;
typedef struct _GstUNIXServerSinkClass GstUNIXServerSinkClass;

typedef enum {
  GST_UNIX_SERVER_SINK_OPEN             = (GST_ELEMENT_FLAG_LAST << 0),

  GST_UNIX_SERVER_SINK_FLAG_LAST        = (GST_ELEMENT_FLAG_LAST << 2)
} GstUNIXServerSinkFlags;

/**
 * GstUNIXServerSink:
 *
 * Opaque data structure.
 */
struct _GstUNIXServerSink {
  GstMultiSocketSink element;

  /* socket information */
  gchar *path;

  GSocket *server_socket;
  GSource *server_source;
};

struct _GstUNIXServerSinkClass {
  GstMultiSocketSinkClass parent_class;
};

GType gst_unix_server_sink_get_type (void);

G_END_DECLS

#endif /* __GST_UNIX_SERVER_SINK_H__ */
