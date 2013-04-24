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

#include "gstomx_base_filter.h"
#include "gstomx.h"
#include "gstomx_interface.h"

#include <string.h>             /* for memcpy */

/* MODIFICATION: for state-tuning */
static void output_loop (gpointer data);

enum
{
  ARG_USE_TIMESTAMPS = GSTOMX_NUM_COMMON_PROP,
  ARG_NUM_INPUT_BUFFERS,
  ARG_NUM_OUTPUT_BUFFERS,
};

static void init_interfaces (GType type);
GSTOMX_BOILERPLATE_FULL (GstOmxBaseFilter, gst_omx_base_filter, GstElement,
    GST_TYPE_ELEMENT, init_interfaces);

static inline void
log_buffer (GstOmxBaseFilter * self, OMX_BUFFERHEADERTYPE * omx_buffer, const gchar *name)
{
  GST_DEBUG_OBJECT (self, "%s: omx_buffer: "
      "size=%lu, "
      "len=%lu, "
      "flags=%lu, "
      "offset=%lu, "
      "timestamp=%lld",
      name, omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nFlags,
      omx_buffer->nOffset, omx_buffer->nTimeStamp);
}

/* Add_code_for_extended_color_format */
static gboolean
is_extended_color_format(GstOmxBaseFilter * self, GOmxPort * port)
{
  OMX_PARAM_PORTDEFINITIONTYPE param;
  OMX_HANDLETYPE omx_handle = self->gomx->omx_handle;

  if (G_UNLIKELY (!omx_handle)) {
    GST_WARNING_OBJECT (self, "no component");
    return FALSE;
  }

  G_OMX_INIT_PARAM (param);

  param.nPortIndex = port->port_index;
  OMX_GetParameter (omx_handle, OMX_IndexParamPortDefinition, &param);

  switch ((guint)param.format.video.eColorFormat) {
    case OMX_EXT_COLOR_FormatNV12TPhysicalAddress:
    case OMX_EXT_COLOR_FormatNV12LPhysicalAddress:
    case OMX_EXT_COLOR_FormatNV12Tiled:
    case OMX_EXT_COLOR_FormatNV12TFdValue:
    case OMX_EXT_COLOR_FormatNV12LFdValue:
      return TRUE;
    default:
      return FALSE;
  }
}

static void
setup_ports (GstOmxBaseFilter * self)
{
  /* Input port configuration. */
  g_omx_port_setup (self->in_port);
  gst_pad_set_element_private (self->sinkpad, self->in_port);

  /* Output port configuration. */
  g_omx_port_setup (self->out_port);
  gst_pad_set_element_private (self->srcpad, self->out_port);

  /* @todo: read from config file: */
  if (g_getenv ("OMX_ALLOCATE_ON")) {
    GST_DEBUG_OBJECT (self, "OMX_ALLOCATE_ON");
    self->in_port->omx_allocate = TRUE;
    self->out_port->omx_allocate = TRUE;
    self->in_port->shared_buffer = FALSE;
    self->out_port->shared_buffer = FALSE;
  } else if (g_getenv ("OMX_SHARE_HACK_ON")) {
    GST_DEBUG_OBJECT (self, "OMX_SHARE_HACK_ON");
    self->in_port->shared_buffer = TRUE;
    self->out_port->shared_buffer = TRUE;
  } else if (g_getenv ("OMX_SHARE_HACK_OFF")) {
    GST_DEBUG_OBJECT (self, "OMX_SHARE_HACK_OFF");
    self->in_port->shared_buffer = FALSE;
    self->out_port->shared_buffer = FALSE;
  /* MODIFICATION: Add extended_color_format */
  } else if (self->gomx->component_vendor == GOMX_VENDOR_SLSI) {
    self->in_port->shared_buffer = (is_extended_color_format(self, self->in_port))
        ? FALSE : TRUE;
    self->out_port->shared_buffer = (is_extended_color_format(self, self->out_port))
        ? FALSE : TRUE;
  } else if (self->gomx->component_vendor == GOMX_VENDOR_QCT) {
    GST_DEBUG_OBJECT (self, "GOMX_VENDOR_QCT");
    self->in_port->omx_allocate = TRUE;
    self->out_port->omx_allocate = TRUE;
    self->in_port->shared_buffer = FALSE;
    self->out_port->shared_buffer = FALSE;
  } else {
    GST_DEBUG_OBJECT (self, "default sharing and allocation");
  }

  GST_DEBUG_OBJECT (self, "omx_allocate: in: %d, out: %d",
      self->in_port->omx_allocate, self->out_port->omx_allocate);
  GST_DEBUG_OBJECT (self, "share_buffer: in: %d, out: %d",
      self->in_port->shared_buffer, self->out_port->shared_buffer);
}

static GstFlowReturn
omx_change_state(GstOmxBaseFilter * self,GstOmxChangeState transition, GOmxPort *in_port, GstBuffer * buf)
{
  GOmxCore *gomx;
  GstFlowReturn ret = GST_FLOW_OK;

  gomx = self->gomx;

  switch (transition) {
  case GstOmx_LodedToIdle:
    {
      g_mutex_lock (self->ready_lock);

      GST_INFO_OBJECT (self, "omx: prepare");

      /** @todo this should probably go after doing preparations. */
      if (self->omx_setup) {
        self->omx_setup (self);
      }

      setup_ports (self);

      g_omx_core_prepare (self->gomx);

      if (gomx->omx_state == OMX_StateIdle) {
        self->ready = TRUE;
        gst_pad_start_task (self->srcpad, output_loop, self->srcpad);
      }

      g_mutex_unlock (self->ready_lock);

      if (gomx->omx_state != OMX_StateIdle)
        goto out_flushing;
    }
    break;

  case GstOmx_IdleToExcuting:
    {
      GST_INFO_OBJECT (self, "omx: play");
      g_omx_core_start (gomx);

      if (gomx->omx_state != OMX_StateExecuting)
        goto out_flushing;

      /* send buffer with codec data flag */
      /** @todo move to util */
      if (self->codec_data) {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        GST_LOG_OBJECT (self, "request buffer");
        omx_buffer = g_omx_port_request_buffer (in_port);

        if (G_LIKELY (omx_buffer)) {
          omx_buffer->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;

          omx_buffer->nFilledLen = GST_BUFFER_SIZE (self->codec_data);
          memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
          GST_BUFFER_DATA (self->codec_data), omx_buffer->nFilledLen);

          GST_LOG_OBJECT (self, "release_buffer");
          g_omx_port_release_buffer (in_port, omx_buffer);
        }
      }
    }
    break;

  default:
    break;
  }

leave:

  GST_LOG_OBJECT (self, "end");
  return ret;

  /* special conditions */
out_flushing:
  {
    const gchar *error_msg = NULL;

    if (gomx->omx_error) {
      error_msg = "Error from OpenMAX component";
    } else if (gomx->omx_state != OMX_StateExecuting &&
        gomx->omx_state != OMX_StatePause) {
      error_msg = "OpenMAX component in wrong state";
    }

    if (error_msg) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL), ("%s", error_msg));
      ret = GST_FLOW_ERROR;
    }

    if (buf)
      gst_buffer_unref (buf);
    goto leave;
  }
}

static GstStateChangeReturn
change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstOmxBaseFilter *self;
  GOmxCore *core;

  self = GST_OMX_BASE_FILTER (element);
  core = self->gomx;

  GST_LOG_OBJECT (self, "begin");

  GST_INFO_OBJECT (self, "changing state %s - %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_INFO_OBJECT (self, "GST_STATE_CHANGE_NULL_TO_READY");
      if (core->omx_state != OMX_StateLoaded) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto leave;
      }

      if (self->adapter_size > 0) {
        GST_LOG_OBJECT (self, "gst_adapter_new. size: %d", self->adapter_size);
        self->adapter = gst_adapter_new();
        if (self->adapter == NULL)
          GST_ERROR_OBJECT (self, "Failed to create adapter!!");
      }
      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_INFO_OBJECT (self, "GST_STATE_CHANGE_READY_TO_PAUSED");

      core->omx_unrecover_err_cnt = 0;

      /* MODIFICATION: state tuning */
      if (self->use_state_tuning) {
        GST_INFO_OBJECT (self, "use state-tuning feature");
        /* to handle abnormal state change. */
        if (self->gomx != self->in_port->core) {
          GST_ERROR_OBJECT(self, "self->gomx != self->in_port->core. start new in_port");
          self->in_port = g_omx_core_new_port (self->gomx, 0);
        }
        if (self->gomx != self->out_port->core) {
          GST_ERROR_OBJECT(self, "self->gomx != self->out_port->core. start new out_port");
          self->out_port = g_omx_core_new_port (self->gomx, 1);
        }

        omx_change_state(self, GstOmx_LodedToIdle, NULL, NULL);

        if (core->omx_state != OMX_StateIdle) {
          GST_ERROR_OBJECT(self, "fail to move from OMX state Loaded to Idle");
          g_omx_port_finish(self->in_port);
          g_omx_port_finish(self->out_port);
          g_omx_core_stop(core);
          g_omx_core_unload(core);
          ret = GST_STATE_CHANGE_FAILURE;
          goto leave;
        }
      }
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto leave;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_INFO_OBJECT (self, "GST_STATE_CHANGE_PAUSED_TO_READY");
      g_mutex_lock (self->ready_lock);
      if (self->ready) {
        g_omx_port_finish (self->in_port);
        g_omx_port_finish (self->out_port);

        g_omx_core_stop (core);
        g_omx_core_unload (core);
        self->ready = FALSE;
      }
      g_mutex_unlock (self->ready_lock);
      if (core->omx_state != OMX_StateLoaded &&
          core->omx_state != OMX_StateInvalid) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto leave;
      }
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_INFO_OBJECT (self, "GST_STATE_CHANGE_READY_TO_NULL");
      if (self->adapter) {
        gst_adapter_clear(self->adapter);
        g_object_unref(self->adapter);
        self->adapter = NULL;
      }
      break;

    default:
      break;
  }

leave:
  GST_LOG_OBJECT (self, "end");

  return ret;
}

static void
finalize (GObject * obj)
{
  GstOmxBaseFilter *self;

  self = GST_OMX_BASE_FILTER (obj);

  if (self->adapter) {
    gst_adapter_clear(self->adapter);
    g_object_unref(self->adapter);
    self->adapter = NULL;
  }

  if (self->codec_data) {
    gst_buffer_unref (self->codec_data);
    self->codec_data = NULL;
  }

  g_omx_core_free (self->gomx);

  g_mutex_free (self->ready_lock);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
set_property (GObject * obj,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstOmxBaseFilter *self;

  self = GST_OMX_BASE_FILTER (obj);

  switch (prop_id) {
    case ARG_USE_TIMESTAMPS:
      self->use_timestamps = g_value_get_boolean (value);
      break;
    case ARG_NUM_INPUT_BUFFERS:
    case ARG_NUM_OUTPUT_BUFFERS:
    {
      OMX_PARAM_PORTDEFINITIONTYPE param;
      OMX_HANDLETYPE omx_handle = self->gomx->omx_handle;
      OMX_U32 nBufferCountActual;
      GOmxPort *port = (prop_id == ARG_NUM_INPUT_BUFFERS) ?
          self->in_port : self->out_port;

      if (G_UNLIKELY (!omx_handle)) {
        GST_WARNING_OBJECT (self, "no component");
        break;
      }

      nBufferCountActual = g_value_get_uint (value);

      G_OMX_INIT_PARAM (param);

      param.nPortIndex = port->port_index;
      OMX_GetParameter (omx_handle, OMX_IndexParamPortDefinition, &param);

      if (nBufferCountActual < param.nBufferCountMin) {
        GST_ERROR_OBJECT (self, "buffer count %lu is less than minimum %lu",
            nBufferCountActual, param.nBufferCountMin);
        return;
      }

      param.nBufferCountActual = nBufferCountActual;

      OMX_SetParameter (omx_handle, OMX_IndexParamPortDefinition, &param);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static void
get_property (GObject * obj, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstOmxBaseFilter *self;

  self = GST_OMX_BASE_FILTER (obj);

  if (gstomx_get_property_helper (self->gomx, prop_id, value))
    return;

  switch (prop_id) {
    case ARG_USE_TIMESTAMPS:
      g_value_set_boolean (value, self->use_timestamps);
      break;
    case ARG_NUM_INPUT_BUFFERS:
    case ARG_NUM_OUTPUT_BUFFERS:
    {
      OMX_PARAM_PORTDEFINITIONTYPE param;
      OMX_HANDLETYPE omx_handle = self->gomx->omx_handle;
      GOmxPort *port = (prop_id == ARG_NUM_INPUT_BUFFERS) ?
          self->in_port : self->out_port;

      if (G_UNLIKELY (!omx_handle)) {
        GST_WARNING_OBJECT (self, "no component");
        g_value_set_uint (value, 0);
        break;
      }

      G_OMX_INIT_PARAM (param);

      param.nPortIndex = port->port_index;
      OMX_GetParameter (omx_handle, OMX_IndexParamPortDefinition, &param);

      g_value_set_uint (value, param.nBufferCountActual);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }
}

static void
type_base_init (gpointer g_class)
{
}

static void
type_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  gstelement_class = GST_ELEMENT_CLASS (g_class);

  gobject_class->finalize = finalize;
  gstelement_class->change_state = change_state;

  /* Properties stuff */
  {
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    gstomx_install_property_helper (gobject_class);

    g_object_class_install_property (gobject_class, ARG_USE_TIMESTAMPS,
        g_param_spec_boolean ("use-timestamps", "Use timestamps",
            "Whether or not to use timestamps",
            TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, ARG_NUM_INPUT_BUFFERS,
        g_param_spec_uint ("input-buffers", "Input buffers",
            "The number of OMX input buffers",
            1, 10, 4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject_class, ARG_NUM_OUTPUT_BUFFERS,
        g_param_spec_uint ("output-buffers", "Output buffers",
            "The number of OMX output buffers",
            1, 10, 4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  }
}

static inline GstFlowReturn
push_buffer (GstOmxBaseFilter * self, GstBuffer * buf, OMX_BUFFERHEADERTYPE * omx_buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOmxBaseFilterClass *basefilter_class;

  basefilter_class = GST_OMX_BASE_FILTER_GET_CLASS (self);
  /* process output gst buffer before gst_pad_push */
  if (basefilter_class->process_output_buf) {
    GstOmxReturn ret = GSTOMX_RETURN_OK;
    ret = basefilter_class->process_output_buf(self, &buf, omx_buffer);
    if (ret == GSTOMX_RETURN_SKIP) {
      gst_buffer_unref (buf);
      goto leave;
    }
  }

  /* set average duration for memsink. need to check */
  GST_BUFFER_DURATION(buf) = self->duration;

  GST_LOG_OBJECT (self, "OUT_BUFFER: timestamp = %" GST_TIME_FORMAT " size = %lu",
      GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));
  ret = gst_pad_push (self->srcpad, buf);
  GST_LOG_OBJECT (self, "gst_pad_push end. ret = %d", ret);

leave:
  return ret;
}

static void
output_loop (gpointer data)
{
  GstPad *pad;
  GOmxCore *gomx;
  GOmxPort *out_port;
  GstOmxBaseFilter *self;
  GstFlowReturn ret = GST_FLOW_OK;

  pad = data;
  self = GST_OMX_BASE_FILTER (gst_pad_get_parent (pad));
  gomx = self->gomx;

  GST_LOG_OBJECT (self, "begin");

  /* do not bother if we have been setup to bail out */
  if ((ret = g_atomic_int_get (&self->last_pad_push_return)) != GST_FLOW_OK)
    goto leave;

  if (!self->ready) {
    g_error ("not ready");
    return;
  }

  out_port = self->out_port;

  if (G_LIKELY (out_port->enabled)) {
    OMX_BUFFERHEADERTYPE *omx_buffer = NULL;

    GST_LOG_OBJECT (self, "request buffer");
    omx_buffer = g_omx_port_request_buffer (out_port);

    GST_LOG_OBJECT (self, "omx_buffer: %p", omx_buffer);

    if (G_UNLIKELY (!omx_buffer)) {
      GST_WARNING_OBJECT (self, "null buffer: leaving");
      ret = GST_FLOW_WRONG_STATE;
      goto leave;
    }

    log_buffer (self, omx_buffer, "output_loop");

    if (G_LIKELY (omx_buffer->nFilledLen > 0)) {
      GstBuffer *buf;

#if 1
            /** @todo remove this check */
      if (G_LIKELY (self->in_port->enabled)) {
        GstCaps *caps = NULL;

        caps = gst_pad_get_negotiated_caps (self->srcpad);

        if (!caps) {
                    /** @todo We shouldn't be doing this. */
          GST_WARNING_OBJECT (self, "faking settings changed notification");
          if (gomx->settings_changed_cb)
            gomx->settings_changed_cb (gomx);
        } else {
          GST_LOG_OBJECT (self, "caps already fixed: %" GST_PTR_FORMAT, caps);
          gst_caps_unref (caps);
        }
      }
#endif

      /* buf is always null when the output buffer pointer isn't shared. */
      buf = omx_buffer->pAppPrivate;

            /** @todo we need to move all the caps handling to one single
             * place, in the output loop probably. */
      if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG)) {
        /* modification: to handle both byte-stream and packetized codec_data */
        GstOmxBaseFilterClass *basefilter_class;

        basefilter_class = GST_OMX_BASE_FILTER_GET_CLASS (self);
        if (basefilter_class->process_output_caps) {
          basefilter_class->process_output_caps(self, omx_buffer);
        }
        /* MODIFICATION: to handle output ST12 HW addr (dec) */
      } else if (is_extended_color_format(self, self->out_port)) {
        GstCaps *caps = NULL;
        GstStructure *structure;
        gint width = 0, height = 0;
        SCMN_IMGB *outbuf = NULL;

        if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_DECODEONLY)) {
          GST_INFO_OBJECT (self, "Decodeonly flag was set from component");
          g_omx_port_release_buffer (out_port, omx_buffer);
          goto leave;
        }

        caps = gst_pad_get_negotiated_caps(self->srcpad);
        structure = gst_caps_get_structure(caps, 0);

        gst_structure_get_int(structure, "width", &width);
        gst_structure_get_int(structure, "height", &height);

        if (G_LIKELY((width > 0) && (height > 0))) {
          buf = gst_buffer_new_and_alloc(width * height * 3 / 2);
        } else {
          GST_ERROR_OBJECT (self, "invalid buffer size");
          ret = GST_FLOW_UNEXPECTED;
          goto leave;
        }

        outbuf = (SCMN_IMGB*)(omx_buffer->pBuffer);
        if (outbuf->buf_share_method == 1) {
          GST_LOG_OBJECT (self, "dec output buf: fd[0]:%d  fd[1]:%d fd[2]:%d  w[0]:%d h[0]:%d  buf_share_method:%d", 
              outbuf->fd[0], outbuf->fd[1], outbuf->fd[2], outbuf->w[0], outbuf->h[0], outbuf->buf_share_method);
        } else if (outbuf->buf_share_method == 0) {
          GST_LOG_OBJECT (self, "dec output uses hw addr");
        } else {
          GST_WARNING_OBJECT (self, "dec output buf has wrong buf_share_method");
        }
        memcpy (GST_BUFFER_MALLOCDATA(buf), omx_buffer->pBuffer, omx_buffer->nFilledLen);

        if (self->use_timestamps) {
          GST_BUFFER_TIMESTAMP (buf) =
              gst_util_uint64_scale_int (omx_buffer->nTimeStamp, GST_SECOND,
              OMX_TICKS_PER_SECOND);
        }
        gst_buffer_set_caps(buf, GST_PAD_CAPS(self->srcpad));
        gst_caps_unref (caps);

        ret = push_buffer (self, buf, omx_buffer);
      } else if (buf && !(omx_buffer->nFlags & OMX_BUFFERFLAG_EOS)) {
        GST_BUFFER_SIZE (buf) = omx_buffer->nFilledLen;
        if (self->use_timestamps) {
          GST_BUFFER_TIMESTAMP (buf) =
              gst_util_uint64_scale_int (omx_buffer->nTimeStamp, GST_SECOND,
              OMX_TICKS_PER_SECOND);
        }

        omx_buffer->pAppPrivate = NULL;
        omx_buffer->pBuffer = NULL;

        ret = push_buffer (self, buf, omx_buffer);

      } else {
        /* This is only meant for the first OpenMAX buffers,
         * which need to be pre-allocated. */
        /* Also for the very last one. */
        ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad,
            GST_BUFFER_OFFSET_NONE,
            omx_buffer->nFilledLen, GST_PAD_CAPS (self->srcpad), &buf);

        if (G_LIKELY (buf)) {
          memcpy (GST_BUFFER_DATA (buf),
              omx_buffer->pBuffer + omx_buffer->nOffset,
              omx_buffer->nFilledLen);
          if (self->use_timestamps) {
            GST_BUFFER_TIMESTAMP (buf) =
                gst_util_uint64_scale_int (omx_buffer->nTimeStamp, GST_SECOND,
                OMX_TICKS_PER_SECOND);
          }

          if (self->out_port->shared_buffer) {
            GST_WARNING_OBJECT (self, "couldn't zero-copy");
            /* If pAppPrivate is NULL, it means it was a dummy
             * allocation, free it. */
            if (!omx_buffer->pAppPrivate) {
              g_free (omx_buffer->pBuffer);
              omx_buffer->pBuffer = NULL;
            }
          }

          ret = push_buffer (self, buf, omx_buffer);
        } else {
          GST_WARNING_OBJECT (self, "couldn't allocate buffer of size %lu",
              omx_buffer->nFilledLen);
        }
      }
    } else {
      GST_WARNING_OBJECT (self, "empty buffer");
    }

    if (self->out_port->shared_buffer &&
        !omx_buffer->pBuffer && omx_buffer->nOffset == 0) {
      GstBuffer *buf;
      GstFlowReturn result;

      GST_LOG_OBJECT (self, "allocate buffer");
      result = gst_pad_alloc_buffer_and_set_caps (self->srcpad,
          GST_BUFFER_OFFSET_NONE,
          omx_buffer->nAllocLen, GST_PAD_CAPS (self->srcpad), &buf);

      if (G_LIKELY (result == GST_FLOW_OK)) {
        omx_buffer->pAppPrivate = buf;

        omx_buffer->pBuffer = GST_BUFFER_DATA (buf);
        omx_buffer->nAllocLen = GST_BUFFER_SIZE (buf);
      } else {
        GST_WARNING_OBJECT (self,
            "could not pad allocate buffer, using malloc");
        omx_buffer->pBuffer = g_malloc (omx_buffer->nAllocLen);
      }
    }

    if (self->out_port->shared_buffer && !omx_buffer->pBuffer) {
      GST_ERROR_OBJECT (self, "no input buffer to share");
    }

    if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS)) {
      GST_DEBUG_OBJECT (self, "got eos");
      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
      omx_buffer->nFlags &= ~OMX_BUFFERFLAG_EOS;
      ret = GST_FLOW_UNEXPECTED;
    }

    omx_buffer->nFilledLen = 0;
    GST_LOG_OBJECT (self, "release_buffer");
    g_omx_port_release_buffer (out_port, omx_buffer);
  }

leave:

  self->last_pad_push_return = ret;

  if (gomx->omx_error != OMX_ErrorNone)
    ret = GST_FLOW_ERROR;

  if (ret != GST_FLOW_OK) {
    GST_INFO_OBJECT (self, "pause task, reason:  %s", gst_flow_get_name (ret));
    gst_pad_pause_task (self->srcpad);
  }

  GST_LOG_OBJECT (self, "end");

  gst_object_unref (self);
}

static GstFlowReturn
pad_chain (GstPad * pad, GstBuffer * buf)
{
  GOmxCore *gomx;
  GOmxPort *in_port;
  GstOmxBaseFilter *self;
  GstOmxBaseFilterClass *basefilter_class;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *adapter_buf = NULL;

  self = GST_OMX_BASE_FILTER (GST_OBJECT_PARENT (pad));

  gomx = self->gomx;

  GST_LOG_OBJECT (self, "IN_BUFFER: timestamp = %" GST_TIME_FORMAT " size = %lu, state:%d",
      GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf), gomx->omx_state);

  /* STATE_TUNING */
  if (!self->use_state_tuning) {
    if (G_UNLIKELY (gomx->omx_state == OMX_StateLoaded))
      omx_change_state(self, GstOmx_LodedToIdle, NULL, NULL);
  }

  in_port = self->in_port;

  if (G_LIKELY (in_port->enabled)) {
    guint buffer_offset = 0;
    guint8 *src_data = NULL;
    guint src_size = 0;
    GstClockTime src_timestamp = 0;
    GstClockTime src_duration = 0;

    if (G_UNLIKELY (gomx->omx_state == OMX_StateIdle))
      omx_change_state(self, GstOmx_IdleToExcuting,in_port, buf);

    if (G_UNLIKELY (gomx->omx_state != OMX_StateExecuting)) {
      GST_ERROR_OBJECT (self, "Whoa! very wrong");
    }

    basefilter_class = GST_OMX_BASE_FILTER_GET_CLASS (self);
    /* process input gst buffer before OMX_EmptyThisBuffer */
    if (basefilter_class->process_input_buf)
    {
      GstOmxReturn ret = GSTOMX_RETURN_OK;
      ret = basefilter_class->process_input_buf(self,&buf);
      if (ret == GSTOMX_RETURN_SKIP) {
        gst_buffer_unref(buf);
        goto leave;
      }
    }

    if (self->adapter_size > 0) {
      if (!self->adapter) {
        GST_WARNING_OBJECT (self, "adapter is NULL. retry gst_adapter_new");
        self->adapter = gst_adapter_new();
      }

      if (GST_BUFFER_IS_DISCONT(buf))
      {
        GST_INFO_OBJECT (self, "got GST_BUFFER_IS_DISCONT.");
        gst_adapter_clear(self->adapter);
      }

      gst_adapter_push(self->adapter, buf);

      src_size = gst_adapter_available(self->adapter);
      if (src_size < self->adapter_size) {
        GST_LOG_OBJECT (self, "Not enough data in adapter to feed to decoder.");
        goto leave;
      }

      if (src_size > self->adapter_size) {
        src_size = src_size - GST_BUFFER_SIZE(buf);
        GST_LOG_OBJECT (self, "take buffer from adapter. size=%d", src_size);
      }

      src_timestamp = gst_adapter_prev_timestamp(self->adapter, NULL);
      adapter_buf = gst_adapter_take_buffer(self->adapter, src_size);
      src_data = GST_BUFFER_DATA(adapter_buf);
      src_duration = GST_BUFFER_TIMESTAMP (buf) - src_timestamp;
    } else {
      src_data = GST_BUFFER_DATA (buf);
      src_size = GST_BUFFER_SIZE (buf);
      src_timestamp = GST_BUFFER_TIMESTAMP (buf);
      src_duration = GST_BUFFER_DURATION (buf);
    }

    while (G_LIKELY (buffer_offset < src_size)) {
      OMX_BUFFERHEADERTYPE *omx_buffer;

      if (self->last_pad_push_return != GST_FLOW_OK ||
          !(gomx->omx_state == OMX_StateExecuting ||
              gomx->omx_state == OMX_StatePause)) {
        goto out_flushing;
      }

      GST_LOG_OBJECT (self, "request buffer");
      omx_buffer = g_omx_port_request_buffer (in_port);

      GST_LOG_OBJECT (self, "omx_buffer: %p", omx_buffer);

      if (G_LIKELY (omx_buffer)) {
        log_buffer (self, omx_buffer, "pad_chain");

        /* MODIFICATION: to handle input SN12 HW addr. (enc) */
        if (is_extended_color_format(self, self->in_port)) {
          SCMN_IMGB *inbuf = NULL;

          if (!GST_BUFFER_MALLOCDATA(buf)) {
              GST_WARNING_OBJECT (self, "null MALLOCDATA in hw color format. skip this.");
              goto out_flushing;
          }

          inbuf = (SCMN_IMGB*)(GST_BUFFER_MALLOCDATA(buf));
          if (inbuf != NULL && inbuf->buf_share_method == 1) {
            GST_LOG_OBJECT (self, "enc. fd[0]:%d  fd[1]:%d  fd[2]:%d  w[0]:%d  h[0]:%d   buf_share_method:%d", 
                inbuf->fd[0], inbuf->fd[1], inbuf->fd[2], inbuf->w[0], inbuf->h[0], inbuf->buf_share_method);
          } else if (inbuf != NULL && inbuf->buf_share_method == 0) {
            GST_LOG_OBJECT (self, "enc input buf uses hw addr");
          } else {
            GST_WARNING_OBJECT (self, "enc input buf has wrong buf_share_method");
          }

          memcpy (omx_buffer->pBuffer, GST_BUFFER_MALLOCDATA(buf), sizeof(SCMN_IMGB));
          omx_buffer->nAllocLen = sizeof(SCMN_IMGB);
          omx_buffer->nFilledLen = sizeof(SCMN_IMGB);
        } else if (omx_buffer->nOffset == 0 && self->in_port->shared_buffer) {
          {
            GstBuffer *old_buf;
            old_buf = omx_buffer->pAppPrivate;

            if (old_buf) {
              gst_buffer_unref ((GstBuffer *)old_buf);
            } else if (omx_buffer->pBuffer) {
              g_free (omx_buffer->pBuffer);
              omx_buffer->pBuffer = NULL;
            }
          }

          omx_buffer->pBuffer = src_data;
          omx_buffer->nAllocLen = src_size;
          omx_buffer->nFilledLen = src_size;
          omx_buffer->pAppPrivate = (self->adapter_size > 0) ? (OMX_PTR)adapter_buf : (OMX_PTR)buf;
        } else {
          omx_buffer->nFilledLen = MIN (src_size - buffer_offset,
              omx_buffer->nAllocLen - omx_buffer->nOffset);
          memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
              src_data + buffer_offset, omx_buffer->nFilledLen);
        }

        if (self->use_timestamps) {
          GstClockTime timestamp_offset = 0;

          if (buffer_offset && src_duration != GST_CLOCK_TIME_NONE) {
            timestamp_offset = gst_util_uint64_scale_int (buffer_offset,
                src_duration, src_size);
          }

          omx_buffer->nTimeStamp =
              gst_util_uint64_scale_int (src_timestamp +
              timestamp_offset, OMX_TICKS_PER_SECOND, GST_SECOND);
        }

        /* MODIFICATION: hw addr */
        if (is_extended_color_format(self, self->in_port)) {
          buffer_offset = GST_BUFFER_SIZE (buf);
        } else {
          buffer_offset += omx_buffer->nFilledLen;
        }

        GST_LOG_OBJECT (self, "release_buffer");
                /** @todo untaint buffer */
        g_omx_port_release_buffer (in_port, omx_buffer);
      } else {
        GST_WARNING_OBJECT (self, "null buffer");
        ret = GST_FLOW_WRONG_STATE;
        goto out_flushing;
      }
    }
  } else {
    GST_WARNING_OBJECT (self, "done");
    ret = GST_FLOW_UNEXPECTED;
  }

  if (!self->in_port->shared_buffer) {
    if (self->adapter_size > 0 && adapter_buf) {
      gst_buffer_unref (adapter_buf);
      adapter_buf = NULL;
    } else {
      gst_buffer_unref (buf);
    }
  }

leave:

  GST_LOG_OBJECT (self, "end");

  return ret;

  /* special conditions */
out_flushing:
  {
    const gchar *error_msg = NULL;

    GST_LOG_OBJECT(self, "out_flushing");

    if (gomx->omx_error) {
      error_msg = "Error from OpenMAX component";
    } else if (gomx->omx_state != OMX_StateExecuting &&
        gomx->omx_state != OMX_StatePause) {
      error_msg = "OpenMAX component in wrong state";
    }

    if (error_msg) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL), ("%s", error_msg));
      ret = GST_FLOW_ERROR;
    }

    if (self->adapter_size > 0 && adapter_buf) {
      gst_buffer_unref (adapter_buf);
      adapter_buf = NULL;
    } else {
      gst_buffer_unref (buf);
    }

    goto leave;
  }
}

static gboolean
pad_event (GstPad * pad, GstEvent * event)
{
  GstOmxBaseFilter *self;
  GOmxCore *gomx;
  GOmxPort *in_port;
  gboolean ret = TRUE;

  self = GST_OMX_BASE_FILTER (GST_OBJECT_PARENT (pad));
  gomx = self->gomx;
  in_port = self->in_port;

  GST_LOG_OBJECT (self, "begin");

  GST_INFO_OBJECT (self, "event: %s", GST_EVENT_TYPE_NAME (event));

  if (self->pad_event) {
    if (!self->pad_event(pad, event))
      return TRUE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* if we are init'ed, and there is a running loop; then
       * if we get a buffer to inform it of EOS, let it handle the rest
       * in any other case, we send EOS */
      if (self->ready && self->last_pad_push_return == GST_FLOW_OK) {
        /* send buffer with eos flag */
                /** @todo move to util */
        {
          OMX_BUFFERHEADERTYPE *omx_buffer;

          GST_LOG_OBJECT (self, "request buffer");
          omx_buffer = g_omx_port_request_buffer (in_port);

          if (G_LIKELY (omx_buffer)) {

            if (self->adapter_size > 0 && self->adapter) {
              guint src_len = 0;
              GstBuffer *adapter_buf = NULL;

              src_len = gst_adapter_available(self->adapter);
              if (src_len > 0 && src_len < self->adapter_size) {
                omx_buffer->nTimeStamp = gst_util_uint64_scale_int(
                   gst_adapter_prev_timestamp(self->adapter, NULL),
                   OMX_TICKS_PER_SECOND, GST_SECOND);
                adapter_buf = gst_adapter_take_buffer(self->adapter, src_len);
                omx_buffer->pBuffer = GST_BUFFER_DATA(adapter_buf);
                omx_buffer->nAllocLen = src_len;
                omx_buffer->nFilledLen = src_len;
                omx_buffer->pAppPrivate = adapter_buf;
              }
              gst_adapter_clear(self->adapter);
            }
            omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;

            GST_LOG_OBJECT (self, "release_buffer in EOS. size=%d", omx_buffer->nFilledLen);
            /* foo_buffer_untaint (omx_buffer); */
            g_omx_port_release_buffer (in_port, omx_buffer);
            /* loop handles EOS, eat it here */
            gst_event_unref (event);
            break;
          }
        }
      }

      /* we tried, but it's up to us here */
      ret = gst_pad_push_event (self->srcpad, event);
      break;

    case GST_EVENT_FLUSH_START:
      if (gomx->omx_state == OMX_StatePause || gomx->omx_state == OMX_StateExecuting) {
        gst_pad_push_event (self->srcpad, event);
        self->last_pad_push_return = GST_FLOW_WRONG_STATE;

        g_omx_core_flush_start (gomx);

        gst_pad_pause_task (self->srcpad);

        ret = TRUE;
      } else {
        GST_ERROR_OBJECT (self, "flush start in wrong omx state");
        ret = FALSE;
      }
      break;

    case GST_EVENT_FLUSH_STOP:
      if (gomx->omx_state == OMX_StatePause || gomx->omx_state == OMX_StateExecuting) {
        gst_pad_push_event (self->srcpad, event);
        self->last_pad_push_return = GST_FLOW_OK;

        g_omx_core_flush_stop (gomx);

        if (self->adapter_size > 0 && self->adapter) {
          gst_adapter_clear(self->adapter);
        }

        if (self->ready)
          gst_pad_start_task (self->srcpad, output_loop, self->srcpad);

        ret = TRUE;
      } else {
        GST_ERROR_OBJECT (self, "flush start in wrong omx state");
        ret = FALSE;
      }
      break;

    case GST_EVENT_NEWSEGMENT:
      ret = gst_pad_push_event (self->srcpad, event);
      break;

    default:
      ret = gst_pad_push_event (self->srcpad, event);
      break;
  }

  GST_LOG_OBJECT (self, "end");

  return ret;
}

static gboolean
activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstOmxBaseFilter *self;

  self = GST_OMX_BASE_FILTER (gst_pad_get_parent (pad));

  if (active) {
    GST_DEBUG_OBJECT (self, "activate");
    /* task may carry on */
    g_atomic_int_set (&self->last_pad_push_return, GST_FLOW_OK);

    /* we do not start the task yet if the pad is not connected */
    if (gst_pad_is_linked (pad)) {
      if (self->ready) {
                /** @todo link callback function also needed */
        g_omx_port_resume (self->in_port);
        g_omx_port_resume (self->out_port);

        result = gst_pad_start_task (pad, output_loop, pad);
      }
    }
  } else {
    GST_DEBUG_OBJECT (self, "deactivate");

    /* persuade task to bail out */
    g_atomic_int_set (&self->last_pad_push_return, GST_FLOW_WRONG_STATE);

    if (self->ready) {
            /** @todo disable this until we properly reinitialize the buffers. */
#if 0
      /* flush all buffers */
      OMX_SendCommand (self->gomx->omx_handle, OMX_CommandFlush, OMX_ALL, NULL);
#endif

      /* unlock loops */
      g_omx_port_pause (self->in_port);
      g_omx_port_pause (self->out_port);
    }

    /* make sure streaming finishes */
    result = gst_pad_stop_task (pad);
  }

  gst_object_unref (self);

  return result;
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *self;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  self = GST_OMX_BASE_FILTER (instance);

  GST_LOG_OBJECT (self, "begin");

  self->use_timestamps = TRUE;
  self->use_state_tuning = FALSE;
  self->adapter_size = 0;
  self->adapter = NULL;
  self->duration = 0;

  self->gomx = gstomx_core_new (self, G_TYPE_FROM_CLASS (g_class));
  self->in_port = g_omx_core_new_port (self->gomx, 0);
  self->out_port = g_omx_core_new_port (self->gomx, 1);

  self->ready_lock = g_mutex_new ();

  self->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "sink"), "sink");

  gst_pad_set_chain_function (self->sinkpad, pad_chain);
  gst_pad_set_event_function (self->sinkpad, pad_event);

  self->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "src"), "src");

  gst_pad_set_activatepush_function (self->srcpad, activate_push);

  gst_pad_use_fixed_caps (self->srcpad);

  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  GST_LOG_OBJECT (self, "end");
}

static void
omx_interface_init (GstImplementsInterfaceClass * klass)
{
}

static gboolean
interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_OMX);
  return TRUE;
}

static void
interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = interface_supported;
}

static void
init_interfaces (GType type)
{
  GInterfaceInfo *iface_info;
  GInterfaceInfo *omx_info;


  iface_info = g_new0 (GInterfaceInfo, 1);
  iface_info->interface_init = (GInterfaceInitFunc) interface_init;

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE, iface_info);
  g_free (iface_info);

  omx_info = g_new0 (GInterfaceInfo, 1);
  omx_info->interface_init = (GInterfaceInitFunc) omx_interface_init;

  g_type_add_interface_static (type, GST_TYPE_OMX, omx_info);
  g_free (omx_info);
}
