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
typedef struct GstOmxBaseFilter GstOmxBaseFilter;
typedef struct GstOmxBaseFilterClass GstOmxBaseFilterClass;
typedef void (*GstOmxBaseFilterCb) (GstOmxBaseFilter * self);
typedef gboolean (*GstOmxBaseFilterEventCb) (GstPad * pad, GstEvent * event);


/* using common scmn_imgb format */
#define SCMN_IMGB_MAX_PLANE         (4) /* max channel count */

/* image buffer definition
    +------------------------------------------+ ---
    |                                          |  ^
    |     a[], p[]                             |  |
    |     +---------------------------+ ---    |  |
    |     |                           |  ^     |  |
    |     |<---------- w[] ---------->|  |     |  |
    |     |                           |  |     |  |
    |     |                           |        |
    |     |                           |  h[]   |  e[]
    |     |                           |        |
    |     |                           |  |     |  |
    |     |                           |  |     |  |
    |     |                           |  v     |  |
    |     +---------------------------+ ---    |  |
    |                                          |  v
    +------------------------------------------+ ---

    |<----------------- s[] ------------------>|
*/

typedef struct
{
    int      w[SCMN_IMGB_MAX_PLANE];    /* width of each image plane */
    int      h[SCMN_IMGB_MAX_PLANE];    /* height of each image plane */
    int      s[SCMN_IMGB_MAX_PLANE];    /* stride of each image plane */
    int      e[SCMN_IMGB_MAX_PLANE];    /* elevation of each image plane */
    void   * a[SCMN_IMGB_MAX_PLANE];    /* user space address of each image plane */
    void   * p[SCMN_IMGB_MAX_PLANE];    /* physical address of each image plane, if needs */
    int      cs;    /* color space type of image */
    int      x;    /* left postion, if needs */
    int      y;    /* top position, if needs */
    int      __dummy2;    /* to align memory */
    int      data[16];    /* arbitrary data */

    /* dmabuf fd */
    int fd[SCMN_IMGB_MAX_PLANE];

    /* flag for buffer share */
    int buf_share_method;
} SCMN_IMGB;

/* MODIFICATION: Add extended_color_format */
typedef enum _EXT_OMX_COLOR_FORMATTYPE {
    OMX_EXT_COLOR_FormatNV12TPhysicalAddress = 0x7F000001, /**< Reserved region for introducing Vendor Extensions */
    OMX_EXT_COLOR_FormatNV12LPhysicalAddress = 0x7F000002,
    OMX_EXT_COLOR_FormatNV12Tiled = 0x7FC00002,
    OMX_EXT_COLOR_FormatNV12TFdValue = 0x7F000012,
    OMX_EXT_COLOR_FormatNV12LFdValue = 0x7F000013
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
    GstOmx_LodedToIdle,
    GstOmx_IdleToExcuting
}GstOmxChangeState;

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

  /* MODIFICATION: set output buffer duration as average */
  GstClockTime duration;
};

struct GstOmxBaseFilterClass
{
  GstElementClass parent_class;

  GstOmxReturn (*process_input_buf)(GstOmxBaseFilter *omx_base_filter, GstBuffer **buf);
  GstOmxReturn (*process_output_buf)(GstOmxBaseFilter *omx_base_filter, GstBuffer **buf, OMX_BUFFERHEADERTYPE *omx_buffer);
  void (*process_output_caps)(GstOmxBaseFilter *omx_base_filter, OMX_BUFFERHEADERTYPE *omx_buffer);

};

GType gst_omx_base_filter_get_type (void);

G_END_DECLS
#endif /* GSTOMX_BASE_FILTER_H */
