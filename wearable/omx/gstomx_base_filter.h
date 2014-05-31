/*
 * Copyright (C) 2007-2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef GSTOMX_BASE_FILTER_H
#define GSTOMX_BASE_FILTER_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "gstomx_util.h"
#include <async_queue.h>

G_BEGIN_DECLS
#define GST_OMX_BASE_FILTER_TYPE (gst_omx_base_filter_get_type ())
#define GST_OMX_BASE_FILTER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_OMX_BASE_FILTER_TYPE, GstOmxBaseFilter))
#define GST_OMX_BASE_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_OMX_BASE_FILTER_TYPE, GstOmxBaseFilterClass))
#define GST_OMX_BASE_FILTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_OMX_BASE_FILTER_TYPE, GstOmxBaseFilterClass))

/* MODIFICATION: to guarantee the integrity of data in output buffer */
#define GST_TYPE_GSTOMX_BUFFER               (gst_omx_buffer_get_type())
#define GST_IS_GSTOMX_BUFFER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GSTOMX_BUFFER))

typedef struct GstOmxBaseFilter GstOmxBaseFilter;
typedef struct GstOmxBaseFilterClass GstOmxBaseFilterClass;
typedef void (*GstOmxBaseFilterCb) (GstOmxBaseFilter * self);
typedef gboolean (*GstOmxBaseFilterEventCb) (GstPad * pad, GstEvent * event);

typedef struct GstOmxBuffer GstOmxBuffer;

/* MODIFICATION: check live output buffer count */
#define _OUT_BUFFER_WAIT_TIMEOUT (500 * 1000) /* usec */
#define _DRC_WAIT_TIMEOUT (5 * 1000 * 1000) /* usec */ /* FIXME: we need to know wait time for DRC */

/* MODIFICATION: Add extended_color_format */
typedef enum _EXT_OMX_COLOR_FORMATTYPE {
    OMX_EXT_COLOR_FormatNV12T_Phyaddr_Fd = 0x7F000001, /* Vendor Extensions */
    OMX_EXT_COLOR_FormatNV12L_Phyaddr_Fd = 0x7F000002,
}EXT_OMX_COLOR_FORMATTYPE;

typedef enum GstOmxReturn
{
    GSTOMX_RETURN_OK,
    GSTOMX_RETURN_SKIP,
    GSTOMX_RETURN_ERROR
}GstOmxReturn;

typedef enum GstOmxChangeState
{
    GstOmx_ToLoaded,
    GstOmx_LoadedToIdle,
    GstOmx_IdleToExecuting
}GstOmxChangeState;

typedef enum GstOmxBufferDirection
{
    GSTOMX_BUFFER_DEFAULT,
    GSTOMX_BUFFER_INPUT,
    GSTOMX_BUFFER_INPUT_AFTER_ETB,
    GSTOMX_BUFFER_OUTPUT
}GstOmxBufferDirection;

struct GstOmxBuffer {
  GstBuffer buffer;
  GstOmxBaseFilter *gstomx;
  OMX_BUFFERHEADERTYPE *omx_buffer;
  GstOmxBufferDirection buffer_direction;
};

struct GstOmxBaseFilter
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GOmxCore *gomx;
  GOmxPort *in_port;
  GOmxPort *out_port;

  gboolean use_timestamps;   /** @todo remove; timestamps should always be used */
  gboolean ready;
  GMutex *ready_lock;

  GstOmxBaseFilterCb omx_setup;
  GstOmxBaseFilterEventCb pad_event;
  GstFlowReturn last_pad_push_return;
  GstBuffer *codec_data;

  /* MODIFICATION: state-tuning */
  gboolean use_state_tuning;

  GstAdapter *adapter;  /* adapter */
  guint adapter_size;

#ifdef GSTOMX_HANDLE_NEW_SEGMENT
  /* MODIFICATION : handling new segment event */
  gboolean in_need_segment;
  gboolean out_need_segment;
  GQueue *segment_queue;
#endif

  /* MODIFICATION: check live output buffer count */
  gint num_live_buffers;
  GMutex *buffer_lock;
  GCond *buffer_cond;
  gboolean is_divx_drm;

  /* MODIFICATION: handle eos */
  SCMN_IMGB eos_buffer;

  /* MODIFICATION: set output buffer duration as average */
  GstClockTime duration;

  /* MODIFICATION: wifi display HDCP */
  gboolean bPhysicalOutput;

  /* MODIFICATION: set encoder level*/
  int encoder_level;

  /*MODIFICATION: set encoder profile */
  int encoder_profile;
};

struct GstOmxBaseFilterClass
{
  GstElementClass parent_class;

  GstOmxReturn (*process_input_buf)(GstOmxBaseFilter *omx_base_filter, GstBuffer **buf);
  GstOmxReturn (*process_output_buf)(GstOmxBaseFilter *omx_base_filter, GstBuffer **buf, OMX_BUFFERHEADERTYPE *omx_buffer);
  void (*process_output_caps)(GstOmxBaseFilter *omx_base_filter, OMX_BUFFERHEADERTYPE *omx_buffer);

  void (*instance_init)(GstElement *element);
  void (*instance_deinit)(GstElement *element);

};

GType gst_omx_base_filter_get_type (void);

G_END_DECLS
#endif /* GSTOMX_BASE_FILTER_H */
