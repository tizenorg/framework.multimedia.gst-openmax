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
#include <string.h> /* for memcpy */
#include <unistd.h> /* for close */

/* MODIFICATION: for state-tuning */
static void output_loop (gpointer data);

/* MODIFICATION: for buffer management */
static GstBufferClass *gstomx_buffer_parent_class = NULL;
static void gst_omx_buffer_class_init(gpointer g_class, gpointer class_data);
static void gst_omx_buffer_finalize(GstOmxBuffer *buffer);
static GstOmxBuffer *gst_omx_buffer_new(GstOmxBaseFilter *self);

static void instance_init (GstElement * element);
static void instance_deinit (GstElement * element);

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
      "nAllocLen=%lu, "
      "nFilledLen=%lu, "
      "flags=%lu, "
      "offset=%lu, "
      "timestamp=%lld",
      name, omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nFlags,
      omx_buffer->nOffset, omx_buffer->nTimeStamp);
}


/* MODIFICATION: handle gstomxbuffer unref */
static void
gst_omx_buffer_class_init(gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS(g_class);
  gstomx_buffer_parent_class = g_type_class_peek_parent(g_class);
  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)gst_omx_buffer_finalize;
}

static GType
gst_omx_buffer_get_type(void)
{
  static GType _gst_gstomx_out_buffer_type;

  if (G_UNLIKELY(_gst_gstomx_out_buffer_type == 0)) {
    static const GTypeInfo gstomx_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_omx_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstOmxBuffer),
      0,
      NULL,
      NULL
    };
    _gst_gstomx_out_buffer_type = g_type_register_static(GST_TYPE_BUFFER,
                                                        "GstOmxBuffer",
                                                        &gstomx_buffer_info, 0);
  }
  return _gst_gstomx_out_buffer_type;
}

static void
gst_omx_buffer_finalize(GstOmxBuffer *buffer)
{
  GstOmxBaseFilter *gstomx = buffer->gstomx;
  SCMN_IMGB *outbuf = NULL;
  int i = 0;

  GST_LOG ("gst_buf: %p  omx_buf: %p  ts: %"GST_TIME_FORMAT, buffer, buffer->omx_buffer,
  GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buffer)));

  if (buffer->buffer_direction == GSTOMX_BUFFER_INPUT) {
    /* for input buffer unref case in TZ. add input buf queue and free */
    GST_LOG ("Unref IN TZ buf from other element. push input buf queue. (omx_buf: %p)", buffer->omx_buffer);
    g_omx_port_push_buffer (gstomx->in_port, buffer->omx_buffer);
    goto exit;
  } else if (buffer->buffer_direction == GSTOMX_BUFFER_INPUT_AFTER_ETB) {
    /* for just unref input buffer in TZ. */
    GST_LOG ("Unref IN TZ buf in gst-openmax after ETB. just unref this. (omx_buf: %p)", buffer->omx_buffer);
    goto exit;
  } else if (buffer->buffer_direction == GSTOMX_BUFFER_OUTPUT) {
    /* for output buffer case both normal and TZ. */
    GST_LOG ("Unref OUT buf. (omx_buf: %p)", buffer->omx_buffer);

    g_atomic_int_dec_and_test(&gstomx->num_live_buffers);
    if (gstomx->buffer_cond) {
      g_cond_signal(gstomx->buffer_cond);
    } else {
      GST_ERROR ("we got gst_omx_buffer_finalize. But buffer cond is already NULL!");
    }
  }


  if (gstomx->gomx == NULL) {
    /* FIXME: if we see this log, need to check state with render */
    GST_ERROR ("we got gst_omx_buffer_finalize. But we already free this omx core!!!");
    goto exit;
  }

  if (!(gstomx->gomx->omx_state == OMX_StateExecuting || gstomx->gomx->omx_state == OMX_StatePause)) {
    /* FIXME: if we see this log, need to check state with render */
    GST_ERROR ("we got gst_omx_buffer_finalize at wrong omx state (%d)!!!", gstomx->gomx->omx_state);
    goto exit;
  }

  if (gstomx->out_port->flushing == TRUE) { /* to modify seek issue */
    GST_WARNING ("we are in flushing. do not send to omx component. omx_buf: %p", buffer->omx_buffer);
    g_omx_port_push_buffer (gstomx->out_port, buffer->omx_buffer);
    goto exit;
  }

  /* MODIFICATION: DRC */
  if((gstomx->out_port->core->reconfiguring == GOMX_RECONF_STATE_START ||
    gstomx->out_port->core->reconfiguring == GOMX_RECONF_STATE_PENDING) &&
      (gstomx->out_port->enabled == FALSE)) {

    outbuf = (SCMN_IMGB*)(GST_BUFFER_MALLOCDATA(buffer));

    GST_INFO ("we will finalize buffer but do not fillthisbuffer but just free during reconfiguration");

    for(i = 0; i < gstomx->out_port->num_buffers; i ++) {
      GstOmxSendCmdQueue *gomx_cmd = NULL;

      if (outbuf->fd[0] == gstomx->out_port->scmn_out[i].fd[0]) {
        if (gstomx->out_port->buffers[i] == NULL) {
          GST_INFO ("buffers is NULL");
          goto exit;
        }

      GST_INFO ("send OMX_FreeBuffer"); 
      gomx_cmd = g_malloc(sizeof(GstOmxSendCmdQueue));
      gomx_cmd->type = GSTOMX_COMMAND_FREE_BUFFER;
      gomx_cmd->port = gstomx->out_port->port_index;
      gomx_cmd->omx_buffer = buffer->omx_buffer;
      async_queue_push (gstomx->gomx->cmd.cmd_queue, gomx_cmd);

      if (gstomx->out_port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
        GST_INFO ("tbm_slp_bo_unref: bo[%d] Y plane: %p", i, gstomx->out_port->bo[i].bo_y);
        tbm_bo_unref(gstomx->out_port->bo[i].bo_y);
        gstomx->out_port->bo[i].bo_y = NULL;

        if (gstomx->gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS) {
          GST_INFO ("tbm_slp_bo_unref: bo[%d] UV plane: %p", i, gstomx->out_port->bo[i].bo_uv);
          tbm_bo_unref(gstomx->out_port->bo[i].bo_uv);
          gstomx->out_port->bo[i].bo_uv = NULL;
        }
      }

      break;
      }
    }
  goto exit;
  }

  log_buffer (gstomx, buffer->omx_buffer, "FillThisBuffer");
  buffer->omx_buffer->nFilledLen = 0;
  g_omx_port_release_buffer (gstomx->out_port, buffer->omx_buffer);

exit:
  if (GST_BUFFER_MALLOCDATA(buffer)) {
    g_free(GST_BUFFER_MALLOCDATA(buffer));
    GST_BUFFER_MALLOCDATA(buffer) = NULL;
  }

  if (GST_MINI_OBJECT_CLASS (gstomx_buffer_parent_class)->finalize) {
    GST_MINI_OBJECT_CLASS (gstomx_buffer_parent_class)->finalize (GST_MINI_OBJECT(buffer));
  }
  return;
}

static GstOmxBuffer *
gst_omx_buffer_new(GstOmxBaseFilter *self)
{
  GstOmxBuffer *ret = NULL;

  ret = (GstOmxBuffer *)gst_mini_object_new(GST_TYPE_GSTOMX_BUFFER);
  GST_LOG("creating buffer : %p", ret);

  ret->gstomx = self;
  ret->buffer_direction = GSTOMX_BUFFER_DEFAULT;
  return ret;
}

static void
send_flush_buffer_and_wait(GstOmxBaseFilter *self)
{

  GstCaps *nego_caps = NULL;

  if (self->gomx->output_log_count == 0 && self->gomx->component_vendor  == GOMX_VENDOR_SLSI_EXYNOS) {
    GST_WARNING_OBJECT (self, "do not send FLUSH_BUFFER in this case. just wait for port reconfigure done"); /* only for tizenw */
    goto leave_and_wait;
  }

  nego_caps = gst_pad_get_negotiated_caps (self->srcpad);
  if (nego_caps) {
    GstStructure *structure = NULL;
    gint nego_width = 0;
    gint nego_height = 0;
    GstBuffer *flush_buffer = NULL;

    structure = gst_caps_get_structure (nego_caps, 0);
    gst_structure_get_int(structure, "width", &nego_width);
    gst_structure_get_int(structure, "height", &nego_height);

    flush_buffer = gst_buffer_new_and_alloc (nego_width * nego_height * 3 / 2);

    if (flush_buffer) {
      GstFlowReturn ret = GST_FLOW_OK;
      SCMN_IMGB *outbuf = NULL;

      outbuf = (SCMN_IMGB*)(GST_BUFFER_MALLOCDATA(flush_buffer));
      outbuf->buf_share_method = BUF_SHARE_METHOD_FLUSH_BUFFER; /* 3 */

      outbuf->w[0] = nego_width;
      outbuf->h[0] = nego_height;
      outbuf->w[1] = nego_width;
      outbuf->h[1] = nego_height / 2;
      outbuf->s[0] = ALIGN(nego_width, 128);
      outbuf->e[0] = ALIGN(nego_height, 32);
      outbuf->s[1] = ALIGN(nego_width, 128);
      outbuf->e[1] = ALIGN(nego_height / 2, 16);
      outbuf->a[0] = NULL;
      outbuf->a[1] = NULL;

      gst_buffer_set_caps(flush_buffer, nego_caps);

      GST_WARNING_OBJECT (self, "DRC: gst_pad_push FLUSH_BUFFER");
      ret = gst_pad_push (self->srcpad, flush_buffer);
      GST_WARNING_OBJECT (self, "DRC: gst_pad_push end. ret = %d (%s)", ret, gst_flow_get_name(ret));
    } else {
      GST_ERROR_OBJECT(self, "gst_buffer_new_and_alloc failed");
    }

    gst_caps_unref (nego_caps);
  } else {
    GST_ERROR_OBJECT (self, "gst_pad_get_negotiated_caps has NULL");
  }

leave_and_wait:

  /* now we will wait OMX_CommandPortEnable complete event after sending flush buffer */
  g_mutex_lock(self->gomx->drc_lock);
  {
    GTimeVal abstimeout;

    g_get_current_time(&abstimeout);
    g_time_val_add(&abstimeout, _DRC_WAIT_TIMEOUT);

    if (!g_cond_timed_wait(self->gomx->drc_cond, self->gomx->drc_lock, &abstimeout)) {
      GST_ERROR_OBJECT (self, "DRC wait timeout [%d usec].Skip waiting", _DRC_WAIT_TIMEOUT);
    } else {
      GST_WARNING_OBJECT (self, "DRC done signal received.");
    }
  }
  g_mutex_unlock(self->gomx->drc_lock);

  return;
}

/* Add_code_for_extended_color_format */
static gboolean
is_extended_color_format(GstOmxBaseFilter * self, GOmxPort * port)
{
  if ((self->gomx->codec_type == GSTOMX_CODECTYPE_VIDEO_DEC && port->type == GOMX_PORT_INPUT) ||
    (self->gomx->codec_type == GSTOMX_CODECTYPE_VIDEO_ENC && port->type == GOMX_PORT_OUTPUT)) {
    /* do not need to check decoder input or encoder output color format. */
    return FALSE;
  }

  if (port->output_color_format == OMX_VIDEO_CodingUnused) {
    OMX_PARAM_PORTDEFINITIONTYPE param;
    OMX_HANDLETYPE omx_handle = self->gomx->omx_handle;

    if (G_UNLIKELY (!omx_handle)) {
      GST_WARNING_OBJECT (self, "no component");
      return FALSE;
    }

    G_OMX_INIT_PARAM (param);

    param.nPortIndex = port->port_index;
    OMX_GetParameter (omx_handle, OMX_IndexParamPortDefinition, &param);

    port->output_color_format = param.format.video.eColorFormat;
    GST_INFO_OBJECT (self, "output_color_format is 0x%x from GetParameter", port->output_color_format);
  }

  switch ((guint)port->output_color_format) {
    case OMX_EXT_COLOR_FormatNV12T_Phyaddr_Fd:
    case OMX_EXT_COLOR_FormatNV12L_Phyaddr_Fd:
    case OMX_COLOR_FormatYUV420SemiPlanar:
      return TRUE;
    default:
      GST_ERROR_OBJECT (self, "can not find colorformat(0x%x) from GetParameter. this is non-zero copy. (port: %d)",
          port->output_color_format, port->port_index);
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
  } else if (self->gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS ||
    self->gomx->component_vendor == GOMX_VENDOR_SLSI_SEC) {
    /* MODIFICATION: always share input. enc input buffer unref after EBD. */
    self->in_port->shared_buffer = TRUE;
    self->out_port->shared_buffer = (is_extended_color_format(self, self->out_port))
        ? FALSE : TRUE;

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
  case GstOmx_LoadedToIdle:
    {
      g_mutex_lock (self->ready_lock);

      GST_WARNING_OBJECT (self, "omx: prepare");

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

  case GstOmx_IdleToExecuting:
    {
      GST_WARNING_OBJECT (self, "omx: play");
      g_omx_core_start (gomx);

      if (gomx->omx_state != OMX_StateExecuting)
        goto out_flushing;

      /* send buffer with codec data flag */
      /** @todo move to util */
      if (self->codec_data) {
        if (self->is_divx_drm) {
          /* we do not send codec data in divx drm case. because codec can not parse strd in divx drm. */
          GST_INFO_OBJECT (self, "this is divx drm file. we do not send codec_data(strd) to omx component.");
        }  else {
          OMX_BUFFERHEADERTYPE *omx_buffer;

          GST_LOG_OBJECT (self, "request buffer (codec_data)");
          omx_buffer = g_omx_port_request_buffer (in_port);

          if (G_LIKELY (omx_buffer)) {
            omx_buffer->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;

            omx_buffer->nFilledLen = GST_BUFFER_SIZE (self->codec_data);
            memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
            GST_BUFFER_DATA (self->codec_data), omx_buffer->nFilledLen);

            GST_LOG_OBJECT (self, "release_buffer (codec_data)");
            g_omx_port_release_buffer (in_port, omx_buffer);
          }
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
      if (gomx->post_gst_element_error == FALSE) {
        GST_ERROR_OBJECT (self, "post GST_ELEMENT_ERROR as %s", error_msg);
        GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL), ("%s", error_msg));
        gomx->post_gst_element_error = TRUE;
        ret = GST_FLOW_ERROR;
      } else {
        GST_ERROR_OBJECT (self, "GST_ELEMENT_ERROR is already posted. skip this (%s)", error_msg);
      }
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
  GstOmxBaseFilterClass *basefilter_class;
  GOmxCore *core;

  self = GST_OMX_BASE_FILTER (element);
  core = self->gomx;
  basefilter_class = GST_OMX_BASE_FILTER_GET_CLASS (self);
  GST_LOG_OBJECT (self, "begin");

  GST_WARNING_OBJECT (self, "changing state %s - %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_NULL_TO_READY");
      /*MODIFICATION : for transcode performance move instance init operation here*/
      if (basefilter_class->instance_init && !core)
      {
        GST_WARNING_OBJECT (self, "current state is NULL_TO_READY but core is NULL. instance_init now.");
        basefilter_class->instance_init(element);
      }

      core = self->gomx;
      self->gomx->input_log_count = 0;
      self->gomx->output_log_count = 0;
      core->omx_unrecover_err_cnt = 0;
      core->post_gst_element_error = FALSE;
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
      GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_READY_TO_PAUSED");

      /* MODIFICATION: check live output buffer count */

      g_atomic_int_set(&self->num_live_buffers, 0);

      /* MODIFICATION: state tuning */
      if (self->use_state_tuning) {
        GST_WARNING_OBJECT (self, "use state-tuning feature");
        /* to handle abnormal state change. */
        if (self->gomx != self->in_port->core) {
          GST_ERROR_OBJECT(self, "self->gomx != self->in_port->core. start new in_port");
          self->in_port = g_omx_core_new_port (self->gomx, 0);
        }
        if (self->gomx != self->out_port->core) {
          GST_ERROR_OBJECT(self, "self->gomx != self->out_port->core. start new out_port");
          self->out_port = g_omx_core_new_port (self->gomx, 1);
        }

        if (core->omx_state != OMX_StateLoaded &&
          core->omx_state != OMX_StateInvalid) {
          GST_ERROR_OBJECT(self, "omx is already over OMX_StateIdle");
          ret = GST_STATE_CHANGE_SUCCESS;
          goto leave;
        }

        omx_change_state(self, GstOmx_LoadedToIdle, NULL, NULL);

        if (core->omx_state != OMX_StateIdle) {
          GST_ERROR_OBJECT(self, "fail to move from OMX state Loaded to Idle");
          g_omx_port_finish(self->in_port);
          g_omx_port_finish(self->out_port);
          g_omx_cmd_queue_finish(core);

          g_omx_core_stop(core);
          g_omx_core_unload(core);
          ret = GST_STATE_CHANGE_FAILURE;
          goto leave;
        }
      }
      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto leave;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
      self->gomx->input_log_count = 0;
      self->gomx->output_log_count = 0;
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_PAUSED_TO_READY");
      self->gomx->reconfiguring = GOMX_RECONF_STATE_DEFAULT;
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_READY_TO_NULL");

      /* MODIFICATION: state moved for ipp close */
      g_mutex_lock (self->ready_lock);
      if (self->ready) {
        GST_LOG_OBJECT (self, "port finish and unload omx core");
        g_omx_port_finish (self->in_port);
        g_omx_port_finish (self->out_port);
        g_omx_cmd_queue_finish(core);

        g_omx_core_stop (core);

        /* MODIFICATION: check live output buffer count */
        g_mutex_lock(self->buffer_lock);
        GST_LOG_OBJECT (self, "num_live_buffers = %d", self->num_live_buffers);
        while (self->num_live_buffers > 0) {
          GTimeVal abstimeout;
          GST_ERROR_OBJECT (self, "Wait until all live buffers are released. (Live=%d)", self->num_live_buffers);

          g_get_current_time(&abstimeout);
          g_time_val_add(&abstimeout, _OUT_BUFFER_WAIT_TIMEOUT);

          if (!g_cond_timed_wait(self->buffer_cond, self->buffer_lock, &abstimeout)) {
            GST_ERROR("Buffer wait timeout[%d usec].(Live=%d) Skip waiting...",
              _OUT_BUFFER_WAIT_TIMEOUT, self->num_live_buffers);
            break;
          } else {
            GST_WARNING_OBJECT (self, "Signal received. output buffer returned");
          }
        }
        GST_LOG_OBJECT (self, "Waiting free buffer finished. (Live=%d)", self->num_live_buffers);
        g_mutex_unlock(self->buffer_lock);

        g_omx_core_unload (core);
        self->ready = FALSE;
      }
      g_mutex_unlock (self->ready_lock);
      if (core->omx_state != OMX_StateLoaded &&
          core->omx_state != OMX_StateInvalid) {
        GST_WARNING_OBJECT (self, "GST_STATE_CHANGE_FAILURE");
        ret = GST_STATE_CHANGE_FAILURE;
        goto leave;
      }

      if (self->adapter) {
        gst_adapter_clear(self->adapter);
        g_object_unref(self->adapter);
        self->adapter = NULL;
      }

      self->gomx->input_log_count = 0;
      self->gomx->output_log_count = 0;
      core->omx_unrecover_err_cnt = 0;
      core->post_gst_element_error = FALSE;

      /*MODIFICATION : for transcode performance move instance finalize operation here*/
      if (basefilter_class->instance_deinit)
      {
        basefilter_class->instance_deinit(element);
      }
      break;

    default:
      break;
  }

leave:
  GST_WARNING_OBJECT (self, "change_state end");

  return ret;
}

static void
instance_deinit (GstElement * element)
{
  GstOmxBaseFilter *self;

  self = GST_OMX_BASE_FILTER (element);

  GST_WARNING_OBJECT (self, "begin");
  if (self->adapter) {
    gst_adapter_clear(self->adapter);
    g_object_unref(self->adapter);
    self->adapter = NULL;
  }

  if (self->codec_data) {
    gst_buffer_unref (self->codec_data);
    self->codec_data = NULL;
  }

  /* MODIFICATION : handling new segment event */
#ifdef GSTOMX_HANDLE_NEW_SEGMENT
  self->in_need_segment = FALSE;
  self->out_need_segment = FALSE;
  if (self->segment_queue) {
    g_queue_foreach (self->segment_queue, (GFunc) gst_mini_object_unref, NULL);
    g_queue_free (self->segment_queue);
    self->segment_queue = NULL;
  }
#endif

  g_omx_port_clear(self->gomx);
  g_omx_core_free (self->gomx);
  self->gomx = NULL;

  GST_WARNING_OBJECT (self, "end");
}

static void
finalize (GObject * obj)
{
  GstOmxBaseFilter *self;
  GstOmxBaseFilterClass *basefilter_class;

  self = GST_OMX_BASE_FILTER (obj);
  basefilter_class = GST_OMX_BASE_FILTER_GET_CLASS (self);

  GST_WARNING_OBJECT (self, "finalize enter");

  if (basefilter_class->instance_deinit && self->gomx)
  {
    GST_WARNING_OBJECT (self, "instance_deinit");
    basefilter_class->instance_deinit((GstElement *)obj);
  }

  /* MODIFICATION: check live output buffer count */
  g_cond_free (self->buffer_cond);
  self->buffer_cond = NULL;
  g_mutex_free (self->buffer_lock);

  g_mutex_free (self->ready_lock);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
  GST_LOG_OBJECT(self, "finalize end");
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
  GstOmxBaseFilterClass *basefilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  gstelement_class = GST_ELEMENT_CLASS (g_class);
  basefilter_class = GST_OMX_BASE_FILTER_CLASS (g_class);

  gobject_class->finalize = finalize;
  gstelement_class->change_state = change_state;
  basefilter_class->instance_init = instance_init;
  basefilter_class->instance_deinit = instance_deinit;

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
#ifdef GSTOMX_HANDLE_NEW_SEGMENT
  GstEvent *newsegment = NULL;
  GstFormat format;
  gboolean update;
  gdouble rate, arate;
  gint64 start, stop, pos;

  if (G_UNLIKELY (self->out_need_segment)) {
    newsegment = g_queue_pop_head (self->segment_queue);
    if (newsegment) {
      gst_event_parse_new_segment_full(newsegment, &update, &rate, &arate, &format, &start, &stop, &pos);
      if (GST_CLOCK_DIFF(GST_BUFFER_TIMESTAMP (buf), (GstClockTime)start) > 0) {
        GST_DEBUG_OBJECT (self, "got new segment");
        gst_pad_push_event (self->srcpad, GST_EVENT_CAST (newsegment));
        self->out_need_segment = FALSE;
      }
    }
  }
#endif

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

  if (self->gomx->output_log_count < MAX_DEBUG_FRAME_CNT) {
    GST_WARNING_OBJECT (self, "OUT_BUF[%02d]: gst_buf= %p omx_buf= %p ts= %" GST_TIME_FORMAT " size= %lu",
      self->gomx->output_log_count, buf, omx_buffer, GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));
  } else {
    GST_LOG_OBJECT (self, "OUT_BUF: gst_buf= %p omx_buf= %p ts= %" GST_TIME_FORMAT " size= %lu",
      buf, omx_buffer, GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));
  }

  /* avoid sending buffers which were already sent to the sink */
  if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_TIMESTAMP(buf))) {
    if (self->gomx->hls_streaming && GST_BUFFER_TIMESTAMP(buf) < self->gomx->previous_ts) {
      GST_WARNING_OBJECT (self, "dropping the buffer with ts = %"GST_TIME_FORMAT" and previous_ts = %"GST_TIME_FORMAT,
        GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buf)), GST_TIME_ARGS(self->gomx->previous_ts));
      gst_buffer_unref (buf);
      ret = GST_FLOW_OK;
      goto leave;
    } else {
      self->gomx->previous_ts = GST_BUFFER_TIMESTAMP(buf);
    }
  }

  ret = gst_pad_push (self->srcpad, buf);

  if (self->gomx->output_log_count < MAX_DEBUG_FRAME_CNT) {
    GST_WARNING_OBJECT (self, "gst_pad_push end [%02d]. ret = %d", self->gomx->output_log_count, ret);
    self->gomx->output_log_count++;
  } else {
    GST_LOG_OBJECT (self, "gst_pad_push end. ret = %d", ret);
  }

#ifdef GSTOMX_HANDLE_NEW_SEGMENT
  if (G_UNLIKELY (self->out_need_segment)) {
    if (newsegment) {
      GST_DEBUG_OBJECT (self, "got new segment");
      gst_pad_push_event (self->srcpad, GST_EVENT_CAST (newsegment));
      self->out_need_segment = FALSE;
    }
  }
#endif

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

  if (out_port->flushing == TRUE) {
    GST_WARNING("port is on flushing. go to leave");
    goto leave;
  }

  if (G_LIKELY (out_port->enabled)) {
    OMX_BUFFERHEADERTYPE *omx_buffer = NULL;

    GST_LOG_OBJECT (self, "request buffer");
    omx_buffer = g_omx_port_request_buffer (out_port);

    GST_LOG_OBJECT (self, "omx_buffer: %p", omx_buffer);

    if (G_UNLIKELY (!omx_buffer)) {
      GST_WARNING_OBJECT (self, "null buffer: leaving");

      if (self->gomx->reconfiguring == GOMX_RECONF_STATE_START ||
          self->gomx->reconfiguring == GOMX_RECONF_STATE_PENDING) {
        GST_WARNING_OBJECT (self, "on port setting reconfigure");
        ret = GST_FLOW_OK;
        goto leave;
      }

      ret = GST_FLOW_WRONG_STATE;
      goto leave;
    }

#ifdef GSTOMX_HANDLE_NEW_SEGMENT
    /* MODIFICATION : handling new segment event */
    if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_STARTTIME)) {
      self->out_need_segment = TRUE;
    }
#endif

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

/* [CONDITION 1] encoder OMX_BUFFERFLAG_CODECCONFIG */
      if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG)) {
        /* modification: to handle both byte-stream and packetized codec_data */
        GstOmxBaseFilterClass *basefilter_class;

        GST_WARNING_OBJECT (self, "OMX_BUFFERFLAG_CODECCONFIG flag was set from component. we will set codec_data to src caps.");
        basefilter_class = GST_OMX_BASE_FILTER_GET_CLASS (self);
        if (basefilter_class->process_output_caps) {
          basefilter_class->process_output_caps(self, omx_buffer);
        }

/* [CONDITION 2] MODIFICATION: to handle output ST12 and SN12 HW addr (dec) */
      } else if (is_extended_color_format(self, self->out_port)) {
        GstOmxBuffer *gstomx_buf = NULL;
        GstCaps *caps = NULL, *new_caps = NULL;
        GstStructure *structure;
        gint width = 0, height = 0;
        SCMN_IMGB *outbuf = NULL;

        if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_DECODEONLY)) {
          GST_INFO_OBJECT (self, "Decodeonly flag was set from component");
          g_omx_port_release_buffer (out_port, omx_buffer);
          goto leave;
        }

        caps = gst_pad_get_negotiated_caps(self->srcpad);
        new_caps = gst_caps_make_writable(caps);
        structure = gst_caps_get_structure(new_caps, 0);

        gst_structure_get_int(structure, "width", &width);
        gst_structure_get_int(structure, "height", &height);


        if(self->out_port->core->component_vendor == GOMX_VENDOR_SLSI_EXYNOS)
        {
          gint buffer_index = 0;

          if (omx_buffer->nAllocLen != sizeof(SCMN_IMGB) || omx_buffer->nFilledLen != sizeof(SCMN_IMGB)) {
            GST_ERROR_OBJECT (self, "invalid omx_buffer nAllocLen or nFilledLen. skip this buffer.");
            g_omx_port_release_buffer (out_port, omx_buffer);
            goto leave;
          }

          outbuf = (SCMN_IMGB*)(omx_buffer->pBuffer);

          for (buffer_index = 0; buffer_index < self->out_port->num_buffers; buffer_index++) {
            if(outbuf->fd[0] == self->out_port->scmn_out[buffer_index].fd[0]) {
              GST_LOG_OBJECT (self, "found the correct SCMN_IMGB");
              outbuf->bo[0] = self->out_port->bo[buffer_index].bo_y ;
              outbuf->bo[1] = self->out_port->bo[buffer_index].bo_uv ;
              outbuf->buf_share_method = BUF_SHARE_METHOD_TIZEN_BUFFER;
            }
          }
        } /* GOMX_VENDOR_SLSI_EXYNOS case */
        else if(self->out_port->core->component_vendor == GOMX_VENDOR_SLSI_SEC)
        {
          if (omx_buffer->nAllocLen != sizeof(SCMN_IMGB) || omx_buffer->nFilledLen != sizeof(SCMN_IMGB)) {
            GST_ERROR_OBJECT (self, "invalid omx_buffer nAllocLen or nFilledLen. skip this buffer.");
            g_omx_port_release_buffer (out_port, omx_buffer);
            goto leave;
          }

          outbuf = (SCMN_IMGB*)(omx_buffer->pBuffer);
        } /* GOMX_VENDOR_SLSI_SEC case */
        else
        {
          GST_ERROR_OBJECT (self, "we can not support H/W color format (ST12, SN12) with this vendor (%d)", self->out_port->core->component_vendor);
          goto leave;
        }

        /* MODIFICATION : correct resolution. modify caps if caps and buffer width, height are different */
        if ((width != outbuf->w[0]) || (height != outbuf->h[0])) {
          GST_INFO_OBJECT (self, "resolution does not match, caps:%dx%d but received:%dx%d. so modify caps.", width, height, outbuf->w[0], outbuf->h[0]);
          width = outbuf->w[0];
          height = outbuf->h[0];
          gst_structure_set (structure, "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
        }

        if (G_LIKELY((width > 0) && (height > 0))) {
          /* MODIFICATION: handle gstomxbuffer unref */
          gstomx_buf = gst_omx_buffer_new(self);

          gstomx_buf->omx_buffer = omx_buffer;
          gstomx_buf->buffer_direction = GSTOMX_BUFFER_OUTPUT;
          buf =(GstBuffer *)gstomx_buf;
        } else {
          GST_ERROR_OBJECT (self, "invalid buffer size");
          ret = GST_FLOW_UNEXPECTED;
          goto leave;
        }

        if (outbuf->buf_share_method == BUF_SHARE_METHOD_TIZEN_BUFFER) {
          GST_LOG_OBJECT (self, "dec outbuf TIZEN_BUFFER: bo[0]:%p fd[0]:%d a[0]:%p a[1]:%p w[0]:%d h[0]:%d",
              outbuf->bo[0], outbuf->fd[0], outbuf->a[0], outbuf->a[1], outbuf->w[0], outbuf->h[0]);
        } else if (outbuf->buf_share_method == BUF_SHARE_METHOD_FD) {
          GST_LOG_OBJECT (self, "dec outbuf FD: fd[0]:%d a[0]:%p a[1]:%p w[0]:%d h[0]:%d",
              outbuf->fd[0], outbuf->a[0], outbuf->a[1], outbuf->w[0], outbuf->h[0]);
        } else if (outbuf->buf_share_method == BUF_SHARE_METHOD_PADDR) {
          GST_LOG_OBJECT (self, "dec output buf has H/W addr");
        }

        GST_BUFFER_DATA(buf) = outbuf->a[0]; // we can not free this.
        outbuf->tz_enable = 0;

        GST_BUFFER_SIZE(buf) = width * height * 3 / 2;
        GST_LOG_OBJECT(self, "GST_BUFFER_DATA(buf)=%p, GST_BUFFER_SIZE(buf)=%d", GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
        GST_BUFFER_MALLOCDATA(buf) = g_malloc(width * height * 3 / 2);
        memcpy (GST_BUFFER_MALLOCDATA(buf), outbuf, sizeof(SCMN_IMGB));

        if (self->use_timestamps) {
          GST_BUFFER_TIMESTAMP (buf) =
              gst_util_uint64_scale_int (omx_buffer->nTimeStamp, GST_SECOND,
              OMX_TICKS_PER_SECOND);
        }
        gst_buffer_set_caps(buf, new_caps);
        gst_caps_unref (new_caps);

        /* MODIFICATION: check live output buffer count */
        g_atomic_int_inc(&self->num_live_buffers);

        ret = push_buffer (self, buf, omx_buffer);

/* [CONDITION 4] Output buffer share mode on */
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
/* [CONDITION 5] the First buffers in Output buffer share mode on */

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
      /* temporary fix for DivX when nFilledLen is zero sometimes */
      if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS)) {
        GST_WARNING_OBJECT (self, "EOS Case , so skip to release");
      } else {
        GST_WARNING_OBJECT (self, "release_buffer for empty buffer case");
        omx_buffer->nFilledLen = 0;
        g_omx_port_release_buffer (out_port, omx_buffer);
      }
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
      GST_WARNING_OBJECT (self, "got eos");

      g_omx_port_push_buffer (out_port, omx_buffer);
      GST_WARNING ("in EOS case, push omx buffer (%p) to client output queue. OMX_CommandFlush will be called. port: %d", omx_buffer, out_port->type);

      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
      omx_buffer->nFlags &= ~OMX_BUFFERFLAG_EOS;
      ret = GST_FLOW_UNEXPECTED;
    }

    /* MODIFICATION: handle gstomxbuffer unref */
    if (is_extended_color_format(self, self->out_port) ||omx_buffer->nFilledLen == 0) {
      /* do not release in this case. we already release_buffer in gstbuffer_finalize */
      /* if nFilledLen ==0, we already release this buffer. */
      goto leave;
    } else {
      omx_buffer->nFilledLen = 0;
      GST_LOG_OBJECT (self, "release_buffer");
      g_omx_port_release_buffer (out_port, omx_buffer);
    }

  } else {
     /* out_port->enabled == FLASE */

    if (self->gomx->reconfiguring == GOMX_RECONF_STATE_START) {
      /* we will send BUF_SHARE_METHOD_FLUSH_BUFFER to videosink
       * to return back the previous decoder output buffer (fd) */
      send_flush_buffer_and_wait(self);

      ret = GST_FLOW_OK;
    }
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

  if (self->gomx->input_log_count < MAX_DEBUG_FRAME_CNT) {
    GST_WARNING_OBJECT (self, "IN_BUF[%02d]: gst_buf= %p ts= %" GST_TIME_FORMAT " size= %lu state: %d",
    self->gomx->input_log_count, buf, GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf), gomx->omx_state);
    self->gomx->input_log_count++;
  } else {
    GST_LOG_OBJECT (self, "IN_BUF: gst_buf= %p ts= %" GST_TIME_FORMAT " size= %lu state: %d",
      buf, GST_TIME_ARGS(GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf), gomx->omx_state);
  }

  /* STATE_TUNING */
  if (!self->use_state_tuning) {
    if (G_UNLIKELY (gomx->omx_state == OMX_StateLoaded))
      omx_change_state(self, GstOmx_LoadedToIdle, NULL, NULL);
  }

  in_port = self->in_port;

  if (G_LIKELY (in_port->enabled)) {
    guint buffer_offset = 0;
    guint8 *src_data = NULL;
    guint src_size = 0;
    GstClockTime src_timestamp = 0;
    GstClockTime src_duration = 0;

    if (G_UNLIKELY (gomx->omx_state == OMX_StateIdle))
      omx_change_state(self, GstOmx_IdleToExecuting,in_port, buf);

    if (G_UNLIKELY (gomx->omx_state != OMX_StateExecuting)) {
      GST_ERROR_OBJECT (self, "Whoa! very wrong");
    }

    if (is_extended_color_format(self, self->in_port)) {
      if (!GST_BUFFER_MALLOCDATA(buf)) {
        GST_WARNING_OBJECT (self, "null MALLOCDATA in hw color format. skip this.");
        goto out_flushing;
      }
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
                if (self->gomx->reconfiguring == GOMX_RECONF_STATE_START ||
                    self->gomx->reconfiguring == GOMX_RECONF_STATE_PENDING) {
                    GST_WARNING_OBJECT (self, "on port setting reconf");
                } else {
                    GST_WARNING_OBJECT (self, "go to out_flushing");
                  goto out_flushing;
                }
      }

       if (self->gomx->input_log_count < MAX_DEBUG_FRAME_CNT) {
           GST_WARNING_OBJECT (self, "request buffer");
           omx_buffer = g_omx_port_request_buffer (in_port);
           GST_WARNING_OBJECT (self, "request buffer Done");
      } else{
          GST_LOG_OBJECT (self, "request buffer");
          omx_buffer = g_omx_port_request_buffer (in_port);
          GST_LOG_OBJECT (self, "request buffer Done");
      }

      GST_LOG_OBJECT (self, "omx_buffer: %p", omx_buffer);

      if (G_LIKELY (omx_buffer)) {
        log_buffer (self, omx_buffer, "pad_chain");

        /* MODIFICATION: to handle input SN12 and ST12 HW addr */
        if (is_extended_color_format(self, self->in_port)) {
          SCMN_IMGB *inbuf = NULL;

          if (!GST_BUFFER_MALLOCDATA(buf)) {
              GST_WARNING_OBJECT (self, "null MALLOCDATA in hw color format. skip this.");
              goto out_flushing;
          }

          inbuf = (SCMN_IMGB*)(GST_BUFFER_MALLOCDATA(buf));

          /* make fd from bo to support BUF_SHARE_METHOD_TIZEN_BUFFER */
          if (inbuf != NULL  && inbuf->buf_share_method == BUF_SHARE_METHOD_TIZEN_BUFFER) {
            tbm_bo_handle handle_fd;

            handle_fd = tbm_bo_get_handle (inbuf->bo[0], TBM_DEVICE_MM);
            inbuf->fd[0] = handle_fd.u32;

            /* Exynos send Y & UV by using different bo plane */
            if(self->gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS || self->gomx->component_vendor == GOMX_VENDOR_SLSI_SEC)
            {
              handle_fd = tbm_bo_get_handle (inbuf->bo[1], TBM_DEVICE_MM);
              inbuf->fd[1] = handle_fd.u32;
              memcpy(&self->eos_buffer, inbuf, sizeof(SCMN_IMGB));
            }

            if (inbuf->fd[0] == 0)
              GST_ERROR_OBJECT (self, "input TIZEN_BUFFER with Wrong FD: bo[0]:%p fd[0]:%d a[0]:%p w[0]:%d h[0]:%d",
                  inbuf->bo[0], inbuf->fd[0], inbuf->a[0], inbuf->w[0], inbuf->h[0]);
            else
              GST_LOG_OBJECT (self, "input TIZEN_BUFFER: bo[0]:%p fd[0]:%d a[0]:%p w[0]:%d h[0]:%d",
                  inbuf->bo[0], inbuf->fd[0], inbuf->a[0], inbuf->w[0], inbuf->h[0]);


          } else if (inbuf != NULL  && inbuf->buf_share_method == BUF_SHARE_METHOD_FD) {
            GST_LOG_OBJECT (self, "input FD: fd[0]:%d a[0]:%p w[0]:%d  h[0]:%d",
                inbuf->fd[0], inbuf->a[0], inbuf->w[0], inbuf->h[0]);

            if(self->gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS || self->gomx->component_vendor == GOMX_VENDOR_SLSI_SEC)
              memcpy(&self->eos_buffer, inbuf, sizeof(SCMN_IMGB));

          } else if (inbuf != NULL && inbuf->buf_share_method == BUF_SHARE_METHOD_PADDR) {
            GST_LOG_OBJECT (self, "input PADDR: p[0]:%d a[0]:%p w[0]:%d  h[0]:%d",
                inbuf->p[0], inbuf->a[0], inbuf->w[0], inbuf->h[0], inbuf->buf_share_method);

          } else {
            GST_ERROR_OBJECT (self, "encoder Input buffer has wrong buf_share_method");
          }
            omx_buffer->pBuffer = GST_BUFFER_MALLOCDATA(buf);
            omx_buffer->nAllocLen = sizeof(SCMN_IMGB);
            omx_buffer->nFilledLen = sizeof(SCMN_IMGB);
            /* MODIFICATION: enc input buffer unref after EBD. */
            omx_buffer->pAppPrivate = (OMX_PTR)buf;
        } else if (omx_buffer->nOffset == 0 && self->in_port->shared_buffer) {
          /* MODIFICATION: move unref pAppPrivate code to EBD */
          omx_buffer->pBuffer = src_data;
          omx_buffer->nAllocLen = src_size; /* check this */
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

          /* MODIFICATION: to handle timestamps which are set to -1 in source element */
          if(src_timestamp + timestamp_offset >= G_MAXUINT64)
            omx_buffer->nTimeStamp = G_MAXUINT64;
          else
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

#ifdef GSTOMX_HANDLE_NEW_SEGMENT
        /* MODIFICATION : handling new segment event */
        if (self->in_need_segment) {
          omx_buffer->nFlags |= OMX_BUFFERFLAG_STARTTIME;
          self->in_need_segment = FALSE;
        }
#endif

        GST_LOG_OBJECT (self, "release_buffer");
        /* @todo untaint buffer */
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

    ret = self->last_pad_push_return;
    GST_WARNING_OBJECT(self, "out_flushing: gst_pad_push return: %d (%s)", ret, gst_flow_get_name(ret));

    if (gomx->omx_error) {
      error_msg = "Error from OpenMAX component";
    } else if (gomx->omx_state != OMX_StateExecuting &&
        gomx->omx_state != OMX_StatePause) {
      error_msg = "OpenMAX component in wrong state";
    }

    if (error_msg) {
      if (gomx->post_gst_element_error == FALSE) {
        GST_ERROR_OBJECT (self, "post GST_ELEMENT_ERROR as %s", error_msg);
        GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL), ("%s", error_msg));
        gomx->post_gst_element_error = TRUE;
        ret = GST_FLOW_ERROR;
      } else {
        GST_ERROR_OBJECT (self, "GST_ELEMENT_ERROR is already posted. skip this (%s)", error_msg);
      }
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
      self->gomx->input_log_count = 0;
      self->gomx->output_log_count = 0;

      if (self->ready && self->last_pad_push_return == GST_FLOW_OK)
      {
        /* send buffer with eos flag */
                /** @todo move to util */
                     
        /* S.LSI case, encoder does not send eos bufer. */
        if (gomx->component_vendor == GOMX_VENDOR_SLSI_SEC || gomx->component_vendor == GOMX_VENDOR_SLSI_EXYNOS)
        {
          OMX_BUFFERHEADERTYPE *omx_buffer;

          GST_LOG_OBJECT (self, "request buffer");
          omx_buffer = g_omx_port_request_buffer (in_port);

          if (G_LIKELY (omx_buffer))
          {
            if (gomx->codec_type == GSTOMX_CODECTYPE_AUDIO_DEC)
            {
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
            }
            else if (gomx->codec_type == GSTOMX_CODECTYPE_VIDEO_ENC)
            {
              SCMN_IMGB* inbuf = NULL;
              omx_buffer->pBuffer = (OMX_U8*) g_malloc(sizeof(SCMN_IMGB));

              memcpy(omx_buffer->pBuffer, &self->eos_buffer, sizeof(SCMN_IMGB));
              inbuf = (SCMN_IMGB*)omx_buffer->pBuffer;

              GST_INFO_OBJECT (self, "EOS buffer: fd[0]:%d  fd[1]:%d  fd[2]:%d  a[0]:%p a[1]:%p w[0]:%d  h[0]:%d   buf_share_method:%d",
              inbuf->fd[0], inbuf->fd[1], inbuf->fd[2], inbuf->a[0], inbuf->a[1], inbuf->w[0], inbuf->h[0], inbuf->buf_share_method);
              omx_buffer->nFilledLen = 0;
              omx_buffer->pAppPrivate = NULL;
            }
            else if (gomx->codec_type == GSTOMX_CODECTYPE_VIDEO_DEC)
            {
              omx_buffer->nFilledLen = 0;
              omx_buffer->pAppPrivate = NULL;
            }
            omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;

            GST_WARNING_OBJECT (self, "release_buffer in EOS. omx_buf= %p size= %d", omx_buffer, omx_buffer->nFilledLen);
            /* foo_buffer_untaint (omx_buffer); */
            g_omx_port_release_buffer (in_port, omx_buffer);
            /* loop handles EOS, eat it here */
            gst_event_unref (event);
            break;
          }

        }
      }

      GST_WARNING_OBJECT (self, "gst_pad_push_event : EOS");
      /* we tried, but it's up to us here */
      ret = gst_pad_push_event (self->srcpad, event);
      break;

    case GST_EVENT_FLUSH_START:
      self->gomx->input_log_count = 0;
      self->gomx->output_log_count = 0;
      if (gomx->omx_state == OMX_StatePause || gomx->omx_state == OMX_StateExecuting) {
        gst_pad_push_event (self->srcpad, event);
        self->last_pad_push_return = GST_FLOW_WRONG_STATE;

        g_omx_core_flush_start (gomx);

        gst_pad_pause_task (self->srcpad);

        GST_WARNING_OBJECT (self, "GST_EVENT_FLUSH_START");

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
        self->gomx->previous_ts = 0;

        g_omx_core_flush_stop (gomx);

        if (self->adapter_size > 0 && self->adapter) {
          gst_adapter_clear(self->adapter);
        }

        if (self->ready)
          gst_pad_start_task (self->srcpad, output_loop, self->srcpad);

        GST_WARNING_OBJECT (self, "GST_EVENT_FLUSH_STOP");
        ret = TRUE;
      } else {
        GST_ERROR_OBJECT (self, "flush start in wrong omx state");
        ret = FALSE;
      }
      break;

    case GST_EVENT_NEWSEGMENT:
#ifdef GSTOMX_HANDLE_NEW_SEGMENT
      /* MODIFICATION : handling new segment event */
      if (gomx->omx_state == OMX_StatePause || gomx->omx_state == OMX_StateExecuting) {
        g_queue_push_tail (self->segment_queue, event);
        self->in_need_segment = TRUE;
      } else {
        ret = gst_pad_push_event (self->srcpad, event);
      }
#else
      GST_WARNING_OBJECT (self, "GST_EVENT_NEWSEGMENT");
      ret = gst_pad_push_event (self->srcpad, event);
#endif
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
instance_init (GstElement *element)
{
  GstOmxBaseFilter *self;

  self = GST_OMX_BASE_FILTER (element);

  GST_WARNING_OBJECT (self, "begin");

  self->use_timestamps = TRUE;
  self->use_state_tuning = FALSE;
  self->adapter_size = 0;
  self->adapter = NULL;
  self->is_divx_drm = FALSE;

  self->gomx = gstomx_core_new (self, G_TYPE_FROM_CLASS (GST_OMX_BASE_FILTER_GET_CLASS(element)));
  self->gomx->drc_cond = g_cond_new (); /* for DRC cond wait */
  self->gomx->drc_lock = g_mutex_new (); /* for DRC cond wait */

  self->in_port = g_omx_core_new_port (self->gomx, 0);
  self->out_port = g_omx_core_new_port (self->gomx, 1);
  self->gomx->codec_type = GSTOMX_CODECTYPE_DEFAULT;

#ifdef GSTOMX_HANDLE_NEW_SEGMENT
  /* MODIFICATION : handling new segment event */
  self->segment_queue = g_queue_new ();
#endif
  GST_LOG_OBJECT (self, "end");
}

static void
type_instance_init (GTypeInstance * instance, gpointer g_class)
{
  GstOmxBaseFilter *self;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (g_class);

  self = GST_OMX_BASE_FILTER (instance);

  GST_WARNING_OBJECT (self, "begin");

  self->use_timestamps = TRUE;
  self->use_state_tuning = FALSE;
  self->adapter_size = 0;
  self->adapter = NULL;
  self->is_divx_drm = FALSE;

  self->gomx = gstomx_core_new (self, G_TYPE_FROM_CLASS (g_class));
  self->gomx->drc_cond = g_cond_new (); /* for DRC cond wait */
  self->gomx->drc_lock = g_mutex_new (); /* for DRC cond wait */

  self->in_port = g_omx_core_new_port (self->gomx, 0);
  self->out_port = g_omx_core_new_port (self->gomx, 1);
  self->gomx->codec_type = GSTOMX_CODECTYPE_DEFAULT;
  self->ready_lock = g_mutex_new ();
  /* MODIFICATION: check live output buffer count */
  self->buffer_lock = g_mutex_new ();
  self->buffer_cond = g_cond_new ();

  /* MODIFICATION: flags to avoid sending repeated frames to sink in HLS case */
  self->gomx->hls_streaming = FALSE;
  self->gomx->previous_ts = 0;

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

#ifdef GSTOMX_HANDLE_NEW_SEGMENT
    /* MODIFICATION : handling new segment event */
    self->segment_queue = g_queue_new ();
#endif

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
