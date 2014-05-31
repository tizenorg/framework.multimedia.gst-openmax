/*
 * Copyright (C) 2006-2007 Texas Instruments, Incorporated
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

#include "gstomx_util.h"
#include <dlfcn.h>

#include "gstomx.h"

GST_DEBUG_CATEGORY (gstomx_util_debug);

/*
 * Forward declarations
 */

static inline void change_state (GOmxCore * core, OMX_STATETYPE state);

static inline void wait_for_state (GOmxCore * core, OMX_STATETYPE state);

static inline void
in_port_cb (GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer);

static inline void
out_port_cb (GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer);

static inline void
got_buffer (GOmxCore * core,
    GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer);

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE omx_handle,
    OMX_PTR app_data,
    OMX_EVENTTYPE event, OMX_U32 data_1, OMX_U32 data_2, OMX_PTR event_data);

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE omx_handle,
    OMX_PTR app_data, OMX_BUFFERHEADERTYPE * omx_buffer);

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE omx_handle,
    OMX_PTR app_data, OMX_BUFFERHEADERTYPE * omx_buffer);

static inline const char *omx_state_to_str (OMX_STATETYPE omx_state);

static inline const char *omx_error_to_str (OMX_ERRORTYPE omx_error);

static inline GOmxPort *get_port (GOmxCore * core, guint index);

static void core_deinit (GOmxCore * core);

static inline void port_free_buffers (GOmxPort * port);

static inline void port_allocate_buffers (GOmxPort * port);

static inline void port_start_buffers (GOmxPort * port);

static void PostCommand (gpointer data);

static OMX_CALLBACKTYPE callbacks =
    { EventHandler, EmptyBufferDone, FillBufferDone };

/* protect implementations hash_table */
static GMutex *imp_mutex;
static GHashTable *implementations;
static gboolean initialized;

/* Modification: calculate plane */
/* this is for S.LSI */
static int calc_plane(int width, int height) /* for h.264 */
{
    int mbX, mbY;

    mbX = ALIGN(width, S5P_FIMV_NV12MT_HALIGN);
    mbY = ALIGN(height, S5P_FIMV_NV12MT_VALIGN);

    return ALIGN(mbX * mbY, S5P_FIMV_DEC_BUF_ALIGN);
}


static int calc_yplane(int width, int height)
{
    int mbX, mbY;

    mbX = ALIGN(width + 24, S5P_FIMV_NV12MT_HALIGN);
    mbY = ALIGN(height + 16, S5P_FIMV_NV12MT_VALIGN);

    return ALIGN(mbX * mbY, S5P_FIMV_DEC_BUF_ALIGN);
}

static int calc_uvplane(int width, int height)
{
    int mbX, mbY;

    mbX = ALIGN(width + 16, S5P_FIMV_NV12MT_HALIGN);
    mbY = ALIGN(height + 4, S5P_FIMV_NV12MT_VALIGN);

    return ALIGN(mbX * mbY, S5P_FIMV_DEC_BUF_ALIGN);
}

static void calc_align_size(GOmxPort * port, int width, int height, guint* y_size, guint* uv_size)
{
  if (port->core->compression_format == OMX_VIDEO_CodingAVC) {
    *y_size = calc_plane(width, height);
    *uv_size = calc_plane(width, height /2);
  } else {
    *y_size = calc_yplane(width, height);
    *uv_size = calc_uvplane(width, height /2);
  }
}

/*
 * Util
 */

static void
g_ptr_array_clear (GPtrArray * array)
{
  guint index;
  for (index = 0; index < array->len; index++)
    array->pdata[index] = NULL;
}

static void
g_ptr_array_insert (GPtrArray * array, guint index, gpointer data)
{
  if (index + 1 > array->len) {
    g_ptr_array_set_size (array, index + 1);
  }

  array->pdata[index] = data;
}

typedef void (*GOmxPortFunc) (GOmxPort * port);

static inline void
core_for_each_port (GOmxCore * core, GOmxPortFunc func)
{
  guint index;

  for (index = 0; index < core->ports->len; index++) {
    GOmxPort *port;

    port = get_port (core, index);

    if (port)
      func (port);
  }
}

/*
 * Main
 */

static GOmxImp *imp_new (const gchar * name);
static void imp_free (GOmxImp * imp);

static GOmxImp *
imp_new (const gchar * name)
{
  GOmxImp *imp;

  imp = g_new0 (GOmxImp, 1);

  /* Load the OpenMAX IL symbols */
  {
    void *handle;

    GST_DEBUG ("loading: %s", name);

    imp->dl_handle = handle = dlopen (name, RTLD_LAZY);

    GST_DEBUG ("dlopen(%s) -> %p", name, handle);

    if (!handle) {
      g_warning ("%s\n", dlerror ());
      g_free (imp);
      return NULL;
    }

    imp->mutex = g_mutex_new ();
    imp->sym_table.init = dlsym (handle, "OMX_Init");
    imp->sym_table.deinit = dlsym (handle, "OMX_Deinit");
    imp->sym_table.get_handle = dlsym (handle, "OMX_GetHandle");
    imp->sym_table.free_handle = dlsym (handle, "OMX_FreeHandle");
  }

  return imp;
}

static void
imp_free (GOmxImp * imp)
{
  if (imp->dl_handle) {
    dlclose (imp->dl_handle);
  }
  g_mutex_free (imp->mutex);
  g_free (imp);
}

static inline GOmxImp *
request_imp (const gchar * name)
{
  GOmxImp *imp = NULL;

  g_mutex_lock (imp_mutex);
  imp = g_hash_table_lookup (implementations, name);
  if (!imp) {
    imp = imp_new (name);
    if (imp)
      g_hash_table_insert (implementations, g_strdup (name), imp);
  }
  g_mutex_unlock (imp_mutex);

  if (!imp)
    return NULL;

  g_mutex_lock (imp->mutex);
  if (imp->client_count == 0) {
    OMX_ERRORTYPE omx_error;
    omx_error = imp->sym_table.init ();
    if (omx_error) {
      g_mutex_unlock (imp->mutex);
      return NULL;
    }
  }
  imp->client_count++;
  g_mutex_unlock (imp->mutex);

  return imp;
}

static inline void
release_imp (GOmxImp * imp)
{
  g_mutex_lock (imp->mutex);
  imp->client_count--;
  if (imp->client_count == 0) {
    imp->sym_table.deinit ();
  }
  g_mutex_unlock (imp->mutex);
}

void
g_omx_init (void)
{
  if (!initialized) {
    /* safe as plugin_init is safe */
    imp_mutex = g_mutex_new ();
    implementations = g_hash_table_new_full (g_str_hash,
        g_str_equal, g_free, (GDestroyNotify) imp_free);
    initialized = TRUE;
  }
}

void
g_omx_deinit (void)
{
  if (initialized) {
    g_hash_table_destroy (implementations);
    g_mutex_free (imp_mutex);
    initialized = FALSE;
  }
}

/*
 * Core
 */

GOmxCore *
g_omx_core_new (void *object)
{
  GOmxCore *core;

  core = g_new0 (GOmxCore, 1);

  core->object = object;
  core->ports = g_ptr_array_new ();

  core->omx_state_condition = g_cond_new ();
  core->omx_state_mutex = g_mutex_new ();

  core->done_sem = g_sem_new ();
  core->flush_sem = g_sem_new ();
  core->port_sem = g_sem_new ();

  core->cmd.cmd_queue = async_queue_new ();
  core->cmd.cmd_queue_enabled = TRUE;

  g_static_rec_mutex_init (&core->cmd.cmd_mutex);

  GST_WARNING ("gst_task_create for PostCommand");
  core->cmd.cmd_task = gst_task_create (PostCommand, core);
  gst_task_set_lock (core->cmd.cmd_task, &core->cmd.cmd_mutex);

  core->omx_state = OMX_StateInvalid;

  return core;
}

void
g_omx_core_free (GOmxCore * core)
{
  GST_WARNING ("g_omx_core_free enter");
  core_deinit (core);

  g_sem_free (core->port_sem);
  g_sem_free (core->flush_sem);
  g_sem_free (core->done_sem);

  g_mutex_free (core->omx_state_mutex);
  g_cond_free (core->omx_state_condition);

  g_mutex_free (core->drc_lock);
  g_cond_free (core->drc_cond);

  gst_task_stop (core->cmd.cmd_task);
  gst_task_join(core->cmd.cmd_task);
  gst_object_unref(core->cmd.cmd_task);

  g_ptr_array_free (core->ports, TRUE);

  g_free (core);
  core = NULL;
  GST_WARNING ("g_omx_core_free leave");
}

void
g_omx_core_init (GOmxCore * core)
{
  GST_DEBUG_OBJECT (core->object, "loading: %s %s (%s)",
      core->component_name,
      core->component_role ? core->component_role : "", core->library_name);

  core->imp = request_imp (core->library_name);

  if (!core->imp)
    return;

  core->omx_error = core->imp->sym_table.get_handle (&core->omx_handle,
      (char *) core->component_name, core, &callbacks);

  GST_DEBUG_OBJECT (core->object, "OMX_GetHandle(&%p) -> %d",
      core->omx_handle, core->omx_error);

  if (!core->omx_error) {
    core->omx_state = OMX_StateLoaded;

    if (core->component_role) {
      OMX_PARAM_COMPONENTROLETYPE param;

      GST_DEBUG_OBJECT (core->object, "setting component role: %s",
          core->component_role);

      G_OMX_INIT_PARAM (param);

      strncpy ((char *) param.cRole, core->component_role,
          OMX_MAX_STRINGNAME_SIZE - 1);

      OMX_SetParameter (core->omx_handle, OMX_IndexParamStandardComponentRole,
          &param);
    }

    /* MODIFICATION: Add_component_vendor */
    if (strncmp(core->component_name+4, "SEC", 3) == 0)
    {
      core->component_vendor = GOMX_VENDOR_SLSI_SEC;
    }
    else if (strncmp(core->component_name+4, "Exynos", 6) == 0)
    {
      core->component_vendor = GOMX_VENDOR_SLSI_EXYNOS;
    }
    else
    {
      core->component_vendor = GOMX_VENDOR_DEFAULT;
    }
  }
}

static void
core_deinit (GOmxCore * core)
{
  if (!core->imp)
    return;

  if (core->omx_state == OMX_StateLoaded || core->omx_state == OMX_StateInvalid) {
    if (core->omx_handle) {
      core->omx_error = core->imp->sym_table.free_handle (core->omx_handle);
      GST_DEBUG_OBJECT (core->object, "OMX_FreeHandle(%p) -> %d",
          core->omx_handle, core->omx_error);
    }
  } else {
    GST_WARNING_OBJECT (core->object, "Incorrect state: %s",
        omx_state_to_str (core->omx_state));
  }

  g_free (core->library_name);
  g_free (core->component_name);
  g_free (core->component_role);
  core->library_name = NULL;
  core->component_name = NULL;
  core->component_role = NULL;

  release_imp (core->imp);
  core->imp = NULL;
}

void
g_omx_core_prepare (GOmxCore * core)
{
  change_state (core, OMX_StateIdle);

  /* Allocate buffers. */
  core_for_each_port (core, port_allocate_buffers);

  wait_for_state (core, OMX_StateIdle);
}

void
g_omx_core_start (GOmxCore * core)
{
  change_state (core, OMX_StateExecuting);
  wait_for_state (core, OMX_StateExecuting);

  if (gst_task_start (core->cmd.cmd_task) == FALSE) {
    GST_ERROR ("Could not start PostCommand task");
  }

  if (core->omx_state == OMX_StateExecuting)
    core_for_each_port (core, port_start_buffers);
}

void
g_omx_core_stop (GOmxCore * core)
{
  if (core->omx_state == OMX_StateExecuting ||
      core->omx_state == OMX_StatePause) {
    change_state (core, OMX_StateIdle);
    wait_for_state (core, OMX_StateIdle);
  }
}

void
g_omx_core_pause (GOmxCore * core)
{
  change_state (core, OMX_StatePause);
  wait_for_state (core, OMX_StatePause);
}

void
g_omx_core_unload (GOmxCore * core)
{
  if (core->omx_state == OMX_StateIdle ||
      core->omx_state == OMX_StateWaitForResources ||
      core->omx_state == OMX_StateInvalid) {
    if (core->omx_state != OMX_StateInvalid)
      change_state (core, OMX_StateLoaded);

    core_for_each_port (core, port_free_buffers);

    if (core->omx_state != OMX_StateInvalid)
      wait_for_state (core, OMX_StateLoaded);
  }
}

void
g_omx_port_clear (GOmxCore * core)
{
  core_for_each_port (core, g_omx_port_free);
  g_ptr_array_clear (core->ports);

  GST_WARNING_OBJECT (core->object, "free command queue");
  async_queue_free (core->cmd.cmd_queue);
}

static inline GOmxPort *
get_port (GOmxCore * core, guint index)
{
  if (G_LIKELY (index < core->ports->len)) {
    return g_ptr_array_index (core->ports, index);
  }

  return NULL;
}

GOmxPort *
g_omx_core_new_port (GOmxCore * core, guint index)
{
  GOmxPort *port = get_port (core, index);

  if (port) {
    GST_WARNING_OBJECT (core->object, "port %d already exists", index);
    return port;
  }

  port = g_omx_port_new (core, index);
  g_ptr_array_insert (core->ports, index, port);

  return port;
}

void
g_omx_core_set_done (GOmxCore * core)
{
  g_sem_up (core->done_sem);
}

void
g_omx_core_wait_for_done (GOmxCore * core)
{
  g_sem_down (core->done_sem);
}

void
g_omx_core_flush_start (GOmxCore * core)
{
  core_for_each_port (core, g_omx_port_pause);
}

void
g_omx_core_flush_stop (GOmxCore * core)
{
  core_for_each_port (core, g_omx_port_flush);
  core_for_each_port (core, g_omx_port_resume);
}

/*
 * Port
 */

gboolean
gst_omx_drm_init(GOmxPort * port)
{
  Display *dpy;
  int eventBase = 0;
  int errorBase = 0;
  int dri2Major = 0;
  int dri2Minor = 0;
  char *driverName = NULL;
  char *deviceName = NULL;
  struct drm_auth auth_arg = {0};

  port->drm_fd = -1;

  GST_INFO_OBJECT (port->core->object, "gst_omx_drm_init enter");

  dpy = XOpenDisplay(0);

  /* DRI2 */
  if (!DRI2QueryExtension(dpy, &eventBase, &errorBase)) {
    GST_ERROR_OBJECT (port->core->object, "failed to DRI2QueryExtension()");
    goto ERROR_CASE;
  }

  if (!DRI2QueryVersion(dpy, &dri2Major, &dri2Minor)) {
    GST_ERROR_OBJECT (port->core->object, "failed to DRI2QueryVersion");
    goto ERROR_CASE;
  }

  if (!DRI2Connect(dpy, RootWindow(dpy, DefaultScreen(dpy)), &driverName, &deviceName)) {
    GST_ERROR_OBJECT(port->core->object,"failed to DRI2Connect");
    goto ERROR_CASE;
  }

  /* get the drm_fd though opening the deviceName */
  port->drm_fd = open(deviceName, O_RDWR);
  if (port->drm_fd < 0) {
    GST_ERROR_OBJECT(port->core->object,"cannot open drm device (%s)", deviceName);
    goto ERROR_CASE;
  }
  GST_INFO("Open drm device : %s, fd(%d)", deviceName, port->drm_fd);

  /* get magic from drm to authentication */
  if (ioctl(port->drm_fd, DRM_IOCTL_GET_MAGIC, &auth_arg)) {
    GST_ERROR_OBJECT(port->core->object,"cannot get drm auth magic");
    close(port->drm_fd);
    port->drm_fd = -1;
    goto ERROR_CASE;
  }

  if (!DRI2Authenticate(dpy, RootWindow(dpy, DefaultScreen(dpy)), auth_arg.magic)) {
    GST_ERROR_OBJECT(port->core->object,"cannot get drm authentication from X");
    close(port->drm_fd);
    port->drm_fd = -1;
    goto ERROR_CASE;
  }

    /* drm slp buffer manager init */
    port->bufmgr = tbm_bufmgr_init (port->drm_fd);
    if (!port->bufmgr)
    {
        GST_ERROR_OBJECT (port->core->object, "fail to init buffer manager");
        close (port->drm_fd);
        port->drm_fd = -1;
        goto  ERROR_CASE;
    }

  XCloseDisplay(dpy);
  free(driverName);
  free(deviceName);

  GST_INFO_OBJECT(port->core->object, "gst_omx_drm_init leave");
  return TRUE;

ERROR_CASE:
  XCloseDisplay(dpy);
  if (!driverName) {
    free(driverName);
  }

  if (!deviceName) {
    free(deviceName);
  }

  return FALSE;
}

void
gst_omx_drm_close(GOmxPort * port)
{
  if (port->bufmgr) {
    GST_LOG_OBJECT (port->core->object, "drm_slp_bufmgr_destroy");
    tbm_bufmgr_deinit (port->bufmgr);
    port->bufmgr = NULL;
  }

  if (port->drm_fd != -1) {
    GST_LOG_OBJECT (port->core->object, "close drm fd");
    close (port->drm_fd);
    port->drm_fd = -1;
  }
}

gboolean
gst_omx_drm_alloc_buffer(GOmxPort * port, guint width, guint height, guint buffer_index)
{
  int flag = 0;
  guint y_size = 0, uv_size = 0;
  tbm_bo_handle handle_vaddr;
  tbm_bo_handle handle_fd;

  if (!port->drm_fd) {
    GST_ERROR("drm_fd is NULL");
    return FALSE;
  }

  if (port->core->component_vendor == GOMX_VENDOR_SLSI_EXYNOS) {
    calc_align_size(port, width, height, &y_size, &uv_size);
    flag = TBM_BO_WC;
  }

  /* alloc Y plane*/
  port->bo[buffer_index].bo_y = tbm_bo_alloc (port->bufmgr, y_size, flag);
  if (!port->bo[buffer_index].bo_y) {
    GST_ERROR("failed to tbm_bo_alloc (y plane) size = %d", y_size);
    return FALSE;
  }

  GST_INFO_OBJECT (port->core->object, "buf[%d] Y plane - bo: %p", buffer_index, port->bo[buffer_index].bo_y);

  /* for sending BO to XvImgSink(QC)*/
  port->scmn_out[buffer_index].bo[0] = port->bo[buffer_index].bo_y;

  handle_fd = tbm_bo_get_handle (port->bo[buffer_index].bo_y, TBM_DEVICE_MM);
  port->scmn_out[buffer_index].fd[0] = handle_fd.u32;

  GST_DEBUG_OBJECT (port->core->object, "  buf[%d] Y plane - fd[0]: %d", buffer_index, port->scmn_out[buffer_index].fd[0]);
  handle_vaddr= tbm_bo_get_handle (port->bo[buffer_index].bo_y, TBM_DEVICE_CPU);

  if (port->buffer_type==GOMX_BUFFER_GEM_VDEC_OUTPUT)
    port->scmn_out[buffer_index].a[0] = handle_vaddr.ptr;

  GST_DEBUG_OBJECT (port->core->object, "  buf[%d] Y plane -  a[0]: %p", buffer_index, port->scmn_out[buffer_index].a[0]);

  /* alloc CbCr plane*/
  port->bo[buffer_index].bo_uv = tbm_bo_alloc (port->bufmgr, uv_size, flag);

  if (!port->bo[buffer_index].bo_uv) {
    GST_ERROR("failed to tbm_bo_alloc (cbcr plane), size = %d", uv_size);
    return FALSE;
  }

  GST_INFO_OBJECT (port->core->object, "buf[%d] CbCr plane - bo: %p", buffer_index, port->bo[buffer_index].bo_uv);

  /* for sending BO to XvImgSink(S.LSI)*/
  port->scmn_out[buffer_index].bo[1] = port->bo[buffer_index].bo_uv;

  handle_fd = tbm_bo_get_handle (port->bo[buffer_index].bo_uv, TBM_DEVICE_MM);
  port->scmn_out[buffer_index].fd[1] = handle_fd.u32;
  GST_DEBUG_OBJECT (port->core->object, "  buf[%d] CbCr plane - fd[1]: %d", buffer_index, port->scmn_out[buffer_index].fd[1]);

  handle_vaddr= tbm_bo_get_handle (port->bo[buffer_index].bo_uv, TBM_DEVICE_CPU);
  port->scmn_out[buffer_index].a[1] = handle_vaddr.ptr;
  GST_DEBUG_OBJECT (port->core->object, "  buf[%d] CbCr plane -  a[1]: %p\n", buffer_index, port->scmn_out[buffer_index].a[1]);

  return TRUE;
}

/**
 * note: this is not intended to be called directly by elements (which should
 * instead use g_omx_core_new_port())
 */
GOmxPort *
g_omx_port_new (GOmxCore * core, guint index)
{
  GOmxPort *port;
  port = g_new0 (GOmxPort, 1);

  port->core = core;
  port->port_index = index;
  port->num_buffers = 0;
  port->buffer_size = 0;
  port->buffers = NULL;
  port->shared_buffer = FALSE;

  port->enabled = TRUE;
  port->flushing = FALSE;
  port->queue = async_queue_new ();
  port->mutex = g_mutex_new ();
#ifdef GEM_BUFFER
  port->bufmgr = NULL;
  port->tzmem_fd = -1;
  port->buffer_type = GOMX_BUFFER_DEFAULT;
#endif
  port->output_color_format = OMX_VIDEO_CodingUnused;

  return port;
}

void
g_omx_port_free (GOmxPort * port)
{
  g_mutex_free (port->mutex);
  async_queue_free (port->queue);

  g_free (port->buffers);
  port->buffers = NULL;
  g_free (port);
  port = NULL;
}

void
g_omx_port_setup (GOmxPort * port)
{
  GOmxPortType type = -1;
  OMX_PARAM_PORTDEFINITIONTYPE param;

  G_OMX_INIT_PARAM (param);

  param.nPortIndex = port->port_index;
  OMX_GetParameter (port->core->omx_handle, OMX_IndexParamPortDefinition,
      &param);

  switch (param.eDir) {
    case OMX_DirInput:
      type = GOMX_PORT_INPUT;
      break;
    case OMX_DirOutput:
      type = GOMX_PORT_OUTPUT;
      break;
    default:
      break;
  }

  port->type = type;
    /** @todo should it be nBufferCountMin? */
  port->num_buffers = param.nBufferCountActual;
  port->buffer_size = param.nBufferSize;

  GST_DEBUG_OBJECT(port->core->object, "width:%d, height:%d, buffer size:%d", param.format.video.nFrameWidth, param.format.video.nFrameHeight, port->buffer_size);
  if (port->type == GOMX_PORT_OUTPUT) {
      if (port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
        GST_DEBUG_OBJECT (port->core->object, "gst_omx_drm_init");
        gst_omx_drm_init(port);
      }
  }

  GST_DEBUG_OBJECT (port->core->object,
      "type=%d, num_buffers=%d, buffer_size=%ld, port_index=%d",
      port->type, port->num_buffers, port->buffer_size, port->port_index);

  g_free (port->buffers);
  port->buffers = g_new0 (OMX_BUFFERHEADERTYPE *, port->num_buffers);
}

static void
port_allocate_buffers (GOmxPort * port)
{
  guint buf_idx;
  gsize size;
  OMX_PARAM_PORTDEFINITIONTYPE param;
  guint width, height;

  G_OMX_INIT_PARAM (param);
  param.nPortIndex = port->port_index;
  OMX_GetParameter (port->core->omx_handle, OMX_IndexParamPortDefinition, &param);

  width = param.format.video.nFrameWidth;
  height = param.format.video.nFrameHeight;

  port->num_buffers = param.nBufferCountActual;
  port->buffer_size = param.nBufferSize;
  size = port->buffer_size;

  GST_DEBUG_OBJECT (port->core->object,"port index: %d, num_buffer:%d, buffer_size :%d",
      port->port_index, port->num_buffers, port->buffer_size);

  if(port->buffers != NULL) {
    g_free (port->buffers);
    port->buffers = NULL;
  }
  GST_DEBUG_OBJECT (port->core->object,"make new OMX_BUFFERHEADERTYPE.");
  port->buffers = g_new0 (OMX_BUFFERHEADERTYPE *, port->num_buffers);

  /* MODIFICATION: allocate buffer for SLSI Exynos components*/
  if (port->core->component_vendor == GOMX_VENDOR_SLSI_EXYNOS) {

    for (buf_idx = 0; buf_idx < port->num_buffers; buf_idx++) {
      if (port->omx_allocate) {
        /* OMX_AllocateBuffer with normal buffer */
        GST_DEBUG_OBJECT (port->core->object, "%d: OMX_AllocateBuffer(), size=%" G_GSIZE_FORMAT, buf_idx, size);

        OMX_AllocateBuffer (port->core->omx_handle, &port->buffers[buf_idx], port->port_index, NULL, size);
      } else {
        /* OMX_UseBuffer with gem or TZ*/
        if (port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
          gpointer buffer_data = NULL;
          gpointer pAppPrivate = NULL;

          if(!gst_omx_drm_alloc_buffer(port, width, height, buf_idx))
            GST_ERROR_OBJECT (port->core->object,"gst_omx_drm_alloc_buffer failed");

          buffer_data = &(port->scmn_out[buf_idx]);
          size = sizeof(SCMN_IMGB);

          GST_DEBUG_OBJECT (port->core->object, "%d: OMX_UseBuffer(), (GEM) size=%" G_GSIZE_FORMAT" (%p)",
              buf_idx, size, buffer_data);

          OMX_UseBuffer (port->core->omx_handle, &port->buffers[buf_idx], port->port_index, pAppPrivate, size, buffer_data);
        } else {
          /* OMX_UseBuffer with  normal buffer */
          gpointer buffer_data;
          buffer_data = g_malloc (size);
          GST_DEBUG_OBJECT (port->core->object, "%d: OMX_UseBuffer(), size=%" G_GSIZE_FORMAT" (%p)", buf_idx, size, buffer_data);
          OMX_UseBuffer (port->core->omx_handle, &port->buffers[buf_idx], port->port_index, NULL, size, buffer_data);

          if (port->type == GOMX_PORT_INPUT && port->shared_buffer == TRUE) {
            port->initial_pbuffer[buf_idx] = buffer_data;
            GST_DEBUG_OBJECT (port->core->object, "alloc initial pbuffer. (%d): p= %p", buf_idx, port->initial_pbuffer[buf_idx]);
          }
        }

      }
    }
  }
  else {
    /* allocate buffer for other components */
    for (buf_idx = 0; buf_idx < port->num_buffers; buf_idx++) {
      if (port->omx_allocate) {
        GST_DEBUG_OBJECT (port->core->object,
            "%d: OMX_AllocateBuffer(), size=%" G_GSIZE_FORMAT, buf_idx, size);
        OMX_AllocateBuffer (port->core->omx_handle, &port->buffers[buf_idx],
            port->port_index, NULL, size);
      } else {
        gpointer buffer_data;
        buffer_data = g_malloc (size);
        GST_DEBUG_OBJECT (port->core->object,
            "%d: OMX_UseBuffer(), size=%" G_GSIZE_FORMAT" (%p)", buf_idx, size, buffer_data);
        OMX_UseBuffer (port->core->omx_handle, &port->buffers[buf_idx],
            port->port_index, NULL, size, buffer_data);

        if (port->type == GOMX_PORT_INPUT && port->shared_buffer == TRUE) {
          port->initial_pbuffer[buf_idx] = buffer_data;
          GST_DEBUG_OBJECT (port->core->object,
            "     alloc initial pbuffer. (%d): p= %p", buf_idx, port->initial_pbuffer[buf_idx]);
        }
      }
    }
  }
}

static void
port_free_buffers (GOmxPort * port)
{
  guint i = 0;

  if (port->type == GOMX_PORT_INPUT) {
    GST_WARNING_OBJECT(port->core->object, "Input port free buffers.port->num_buffers = %d", port->num_buffers);
  } else {
    GST_WARNING_OBJECT(port->core->object, "Output port free buffers. port->num_buffers = %d", port->num_buffers);
  }

  for (i = 0; i < port->num_buffers; i++) {
    OMX_BUFFERHEADERTYPE *omx_buffer;
    omx_buffer = port->buffers[i];
    /* Exynos case */
    if (port->core->component_vendor == GOMX_VENDOR_SLSI_EXYNOS) {
      if(port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
        GST_WARNING_OBJECT(port->core->object, "gem buffer free. port %d: bo[%d] Y: %p, UV: %p",port->type, i, port->bo[i].bo_y, port->bo[i].bo_uv);
        tbm_bo_unref(port->bo[i].bo_y);
        port->bo[i].bo_y = NULL;
        tbm_bo_unref(port->bo[i].bo_uv);
        port->bo[i].bo_uv = NULL;
      }

      /* this is for input buffer share case. we need to free initial buffers (decoder input) */
      if (port->shared_buffer && port->type == GOMX_PORT_INPUT && port->initial_pbuffer[i] != NULL) {
        GST_DEBUG_OBJECT(port->core->object, " %d: g_free shared input initial buffer (pBuffer) %p", i, port->initial_pbuffer[i]);
        g_free (port->initial_pbuffer[i]);
        port->initial_pbuffer[i] = NULL;
      }

    /* other vendor case */
    }else {
      if (omx_buffer) {
        if (port->shared_buffer) {
          /* Modification: free pAppPrivate when input/output buffer is shared */
          if (port->type == GOMX_PORT_INPUT && port->initial_pbuffer[i] != NULL) {
            GST_DEBUG_OBJECT(port->core->object,
            " %d: g_free shared input initial buffer (pBuffer) %p", i, port->initial_pbuffer[i]);
            g_free (port->initial_pbuffer[i]);
            port->initial_pbuffer[i] = NULL;
          }

          if (!omx_buffer->pAppPrivate && port->type == GOMX_PORT_OUTPUT && omx_buffer->pBuffer) {
            GST_DEBUG_OBJECT(port->core->object,
            " %d: g_free shared buffer (pBuffer) %p", i, omx_buffer->pBuffer);
            g_free (omx_buffer->pBuffer);
            omx_buffer->pBuffer = NULL;
          }

          if (omx_buffer->pAppPrivate) {
            GST_DEBUG_OBJECT(port->core->object,
                " %d: unref shared buffer (pAppPrivate) %p", i, omx_buffer->pAppPrivate);
            gst_buffer_unref(omx_buffer->pAppPrivate);
            omx_buffer->pAppPrivate = NULL;
          }
        } else {
          /* this is not shared buffer */
          if (!port->omx_allocate) {
            /* Modification: free pBuffer allocated in plugin when OMX_UseBuffer.
             * the component shall free only buffer header if it allocated only buffer header.*/
            GST_DEBUG_OBJECT(port->core->object,
                " %d: free buffer (pBuffer) %p", i, omx_buffer->pBuffer);
            if (omx_buffer->pBuffer) {
              g_free (omx_buffer->pBuffer);
              omx_buffer->pBuffer = NULL;
            }
          }
        }
      } else {
        GST_ERROR_OBJECT(port->core->object, "omx_buffer is NULL. port->buffers[%d]", i);
      }
    }

    if (omx_buffer) {
      GST_DEBUG_OBJECT(port->core->object, "OMX_FreeBuffer");
      OMX_FreeBuffer (port->core->omx_handle, port->port_index, omx_buffer);
      port->buffers[i] = NULL;
    }

  }

  if(port->buffers != NULL) {
    g_free (port->buffers); /* need to check */
    port->buffers = NULL;
  }

  if (port->type == GOMX_PORT_OUTPUT) {
      if (port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
        GST_DEBUG_OBJECT (port->core->object, "gst_omx_drm_close");
        gst_omx_drm_close(port);
      }
  }
}

static void
port_start_buffers (GOmxPort * port)
{
  guint i;

  for (i = 0; i < port->num_buffers; i++) {
    OMX_BUFFERHEADERTYPE *omx_buffer;

    omx_buffer = port->buffers[i];

    /* If it's an input port we will need to fill the buffer, so put it in
     * the queue, otherwise send to omx for processing (fill it up). */
    if (port->type == GOMX_PORT_INPUT){
      got_buffer (port->core, port, omx_buffer);
    } else {
      GST_DEBUG_OBJECT(port->core->object, "index %d FillThisBuffer. omx_buffer= %p",i, omx_buffer);
      g_omx_port_release_buffer (port, omx_buffer);
    }
  }
}

void
g_omx_port_push_buffer (GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer)
{
  async_queue_push (port->queue, omx_buffer);
}

OMX_BUFFERHEADERTYPE *
g_omx_port_request_buffer (GOmxPort * port)
{
  return async_queue_pop (port->queue);
}

void
g_omx_port_release_buffer (GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer)
{
  switch (port->type) {
    case GOMX_PORT_INPUT:
      if (port->core->input_log_count < MAX_DEBUG_FRAME_CNT) {
        GST_WARNING_OBJECT(port->core->object, "[EmptyThisBuffer] omx_buf=%p  pBuf= %p, nFill= %d",
          omx_buffer, omx_buffer->pBuffer, omx_buffer->nFilledLen);
      } else {
        GST_LOG_OBJECT(port->core->object, "[EmptyThisBuffer] omx_buf=%p  pBuf= %p, nFill= %d",
          omx_buffer, omx_buffer->pBuffer, omx_buffer->nFilledLen);
      }
      OMX_EmptyThisBuffer (port->core->omx_handle, omx_buffer);
      break;
    case GOMX_PORT_OUTPUT:
      if (port->core->output_log_count < MAX_DEBUG_FRAME_CNT) {
        GST_WARNING_OBJECT(port->core->object, "[FillThisBuffer] omx_buf=%p  pBuf= %p, nAlloc= %d, nFill= %d",
          omx_buffer, omx_buffer->pBuffer, omx_buffer->nAllocLen, omx_buffer->nFilledLen);
      } else {
        GST_LOG_OBJECT(port->core->object, "[FillThisBuffer] omx_buf=%p  pBuf= %p, nAlloc= %d, nFill= %d",
          omx_buffer, omx_buffer->pBuffer, omx_buffer->nAllocLen, omx_buffer->nFilledLen);
      }
      OMX_FillThisBuffer (port->core->omx_handle, omx_buffer);
      break;
    default:
      break;
  }
}

void
g_omx_port_resume (GOmxPort * port)
{
  async_queue_enable (port->queue);
}

void
g_omx_port_pause (GOmxPort * port)
{
  async_queue_disable (port->queue);
}

/* MODIFICATION: clean queue for DRC */
void
g_omx_port_clean (GOmxPort * port)
{
  OMX_BUFFERHEADERTYPE *omx_buffer;

  if (port->type != GOMX_PORT_OUTPUT) {
    GST_ERROR("g_omx_port_clean is only for output port");
    return;
  }

  while ((omx_buffer = async_queue_pop_forced (port->queue))) {
    omx_buffer->nFilledLen = 0;
    GST_LOG_OBJECT(port->core->object, "pop and free buffers from the client queue. omx_buffer= %p", omx_buffer);

    if ((port->core->reconfiguring == GOMX_RECONF_STATE_START||
      port->core->reconfiguring == GOMX_RECONF_STATE_PENDING)&&
        (port->enabled == FALSE)) {
        SCMN_IMGB * buffer = NULL;

        if (port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
            {
                buffer = (SCMN_IMGB*)omx_buffer->pBuffer;
            }

        if (buffer != NULL) {
          int i = 0;
          for(i = 0; i < port->num_buffers ; i ++) {

            if (buffer->fd[0] == port->scmn_out[i].fd[0]) {
              GST_INFO_OBJECT(port->core->object, "tbm_bo_unref: bo[%d] Y plane: %p", i, port->bo[i].bo_y);
              tbm_bo_unref(port->bo[i].bo_y);
              port->bo[i].bo_y = NULL;

              GST_INFO_OBJECT(port->core->object, "tbm_bo_unref: bo[%d] UV plane: %p", i, port->bo[i].bo_uv);
              tbm_bo_unref(port->bo[i].bo_uv);
              port->bo[i].bo_uv = NULL;
              break;
            }
          }
        } else {
          GST_ERROR("pBuffer is NULL!! at omx_buffer= %p", omx_buffer);
        }
      }
      GST_INFO_OBJECT (port->core->object, "send OMX_FreeBuffer");
      OMX_FreeBuffer (port->core->omx_handle, port->port_index, omx_buffer);
    }
  } 
}

void
g_omx_port_flush (GOmxPort * port)
{
  if (port->type == GOMX_PORT_OUTPUT) {
    port->flushing = TRUE; /* to modify seek issue */
  }

  GST_WARNING ("send OMX_CommandFlush. port: %d", port->type);
  OMX_SendCommand (port->core->omx_handle, OMX_CommandFlush, port->port_index, NULL);
  g_sem_down (port->core->flush_sem);
}

void
g_omx_port_enable (GOmxPort * port)
{
  GOmxCore *core;

  core = port->core;

  OMX_SendCommand (core->omx_handle, OMX_CommandPortEnable, port->port_index,
      NULL);
  port_allocate_buffers (port);
  if (core->omx_state != OMX_StateLoaded)
    port_start_buffers (port);
  g_omx_port_resume (port);

  g_sem_down (core->port_sem);
}

void
g_omx_port_disable (GOmxPort * port)
{
  GOmxCore *core;

  core = port->core;

  OMX_SendCommand (core->omx_handle, OMX_CommandPortDisable, port->port_index,
      NULL);
  g_omx_port_pause (port);
  g_omx_port_flush (port);
  port_free_buffers (port);

  g_sem_down (core->port_sem);
}

void
g_omx_port_finish (GOmxPort * port)
{
  port->enabled = FALSE;
  async_queue_disable (port->queue);
}

void
g_omx_cmd_queue_finish (GOmxCore * core)
{
  GST_WARNING ("disable command queue");
  core->cmd.cmd_queue_enabled = FALSE;
  async_queue_disable (core->cmd.cmd_queue);
}

/*
 * Helper functions.
 */

static inline void
change_state (GOmxCore * core, OMX_STATETYPE state)
{
  GST_DEBUG_OBJECT (core->object, "state=%d", state);
  OMX_SendCommand (core->omx_handle, OMX_CommandStateSet, state, NULL);
}

static inline void
complete_change_state (GOmxCore * core, OMX_STATETYPE state)
{
  g_mutex_lock (core->omx_state_mutex);

  core->omx_state = state;
  g_cond_signal (core->omx_state_condition);
  GST_DEBUG_OBJECT (core->object, "state=%d", state);

  g_mutex_unlock (core->omx_state_mutex);
}

static inline void
wait_for_state (GOmxCore * core, OMX_STATETYPE state)
{
  GTimeVal tv;
  gboolean signaled;

  g_mutex_lock (core->omx_state_mutex);
  if (core->omx_error != OMX_ErrorNone) {
    /* MODIFICATION: ignore init fail to stop. */
    if ((core->component_vendor == GOMX_VENDOR_SLSI_EXYNOS ||
      core->component_vendor == GOMX_VENDOR_SLSI_SEC) &&
        core->omx_error == OMX_ErrorMFCInit) {
      GST_LOG_OBJECT (core->object, "ignore init fail when going to stop");
    } else {
      GST_ERROR_OBJECT (core->object, "there is error %s (0x%lx). but we will wait state change", omx_error_to_str (core->omx_error), core->omx_error);
    }
  }

  g_get_current_time (&tv);
  g_time_val_add (&tv, OMX_STATE_CHANGE_TIMEOUT * G_USEC_PER_SEC);

  /* try once */
  if (core->omx_state != state) {
    signaled =
        g_cond_timed_wait (core->omx_state_condition, core->omx_state_mutex,
        &tv);

    if (!signaled) {
      GST_ERROR_OBJECT (core->object, "timed out switching from '%s' to '%s'",
          omx_state_to_str (core->omx_state), omx_state_to_str (state));
    }
  }

  if (core->omx_error != OMX_ErrorNone)
    goto leave;

  if (core->omx_state != state) {
    GST_ERROR_OBJECT (core->object,
        "wrong state received: state=%d, expected=%d", core->omx_state, state);
  }

leave:
  g_mutex_unlock (core->omx_state_mutex);
}

/*
 * Callbacks
 */

static inline void
in_port_cb (GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer)
{
    /** @todo remove this */

  if (!port->enabled)
    return;
}

static inline void
out_port_cb (GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer)
{
    /** @todo remove this */

  if (!port->enabled)
    return;

#if 0
  if (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS) {
    g_omx_port_set_done (port);
    return;
  }
#endif
}

static inline void
got_buffer (GOmxCore * core, GOmxPort * port, OMX_BUFFERHEADERTYPE * omx_buffer)
{
  if (G_UNLIKELY (!omx_buffer)) {
    return;
  }

  if (G_LIKELY (port)) {
    g_omx_port_push_buffer (port, omx_buffer);

    switch (port->type) {
      case GOMX_PORT_INPUT:
        in_port_cb (port, omx_buffer);
        break;
      case GOMX_PORT_OUTPUT:
        out_port_cb (port, omx_buffer);
        break;
      default:
        break;
    }
  }
}

/* MODIFICATION: Port setting changed */
static void
PortSettingsChanged(GOmxCore * core, OMX_U32 port_index)
{
  GOmxPort *port;
  GstOmxSendCmdQueue *gomx_cmd = NULL;

  GST_LOG_OBJECT (core->object, "PortSettingsChanged enter");

  if (core->component_vendor == GOMX_VENDOR_SLSI_SEC) {
    GST_WARNING_OBJECT (core->object, "sec omx component does not handle PortSettingChanged Event. return");
    return;
  }

  if (core->omx_state != OMX_StateExecuting) {
    GST_WARNING_OBJECT (core->object, "we got PortSettingsChanged Event not in Executing state. state = %d", core->omx_state);
    return;
  }

  if (core->reconfiguring == GOMX_RECONF_STATE_PENDING) {
    GST_WARNING_OBJECT (core->object, "output_port_settingchanged_pending is true. return");
    return;
  }

  port = get_port (core, port_index);
  if (port == NULL) {
    GST_ERROR_OBJECT (core->object, "get_port (output port) returned NULL");
    return;
  }

  if (port->enabled != TRUE) { /* FIX ME: need to change port enable checking sequence */
    core->reconfiguring = GOMX_RECONF_STATE_PENDING;
    GST_WARNING_OBJECT (core->object, "output port is not enabled. set output_port_settingchanged_pending and return");
    return;
  }

  core->reconfiguring = GOMX_RECONF_STATE_START;
  port->enabled = FALSE;
  g_omx_port_pause(port);

  /* post OMX_CommandPortDisable */
  gomx_cmd = g_malloc(sizeof(GstOmxSendCmdQueue));
  gomx_cmd->type = GSTOMX_COMMAND_PORT_DISABLE;
  gomx_cmd->port = port->port_index;
  async_queue_push (core->cmd.cmd_queue, gomx_cmd);

  GST_LOG_OBJECT (core->object, "PortSettingsChanged leave.");
}


static void
PostCommand (gpointer data)
{
  GOmxCore *core = (GOmxCore *) data;
  GOmxPort *port;
  GstOmxSendCmdQueue *gomx_cmd = NULL;

  if (data == NULL) {
    GST_ERROR ("core is NULL!");
    return;
  }

  gomx_cmd = (GstOmxSendCmdQueue*)async_queue_pop (core->cmd.cmd_queue);
  if (core->cmd.cmd_queue_enabled == FALSE) {
    GST_WARNING ("command queue is disable. goto leave.");
    goto pause;
  }

  if (gomx_cmd == NULL) {
    GST_ERROR ("gomx_cmd is NULL.");
    goto leave;
  }

  GST_INFO_OBJECT (core->object, "we got SendCommand. cmd: %d, port: %d", gomx_cmd->type, gomx_cmd->port);
  port = get_port (core, gomx_cmd->port);

  if (port == NULL) {
    GST_ERROR ("port is NULL!");
    goto leave;
  }

  switch (gomx_cmd->type) {
    case GSTOMX_COMMAND_PORT_DISABLE:
      GST_WARNING_OBJECT (core->object, "send OMX_CommandPortDisable (port: %d)", port->port_index);
      OMX_SendCommand (core->omx_handle, OMX_CommandPortDisable, port->port_index, NULL);

      /* MODIFICATION: for DRC */
      if (port->type == GOMX_PORT_OUTPUT) {
        g_omx_port_clean (port);
      }
      break;

    case GSTOMX_COMMAND_PORT_ENABLE:
      GST_WARNING_OBJECT (core->object, "send OMX_CommandPortEnable (port: %d)", port->port_index);
      OMX_SendCommand (core->omx_handle, OMX_CommandPortEnable, port->port_index, NULL);

      GST_INFO_OBJECT (core->object, "port_allocate_buffers (port idx: %d)", port->port_index);
      port_allocate_buffers (port);
      break;

    case GSTOMX_COMMAND_FREE_BUFFER: /* this case comes from fillbufferdone */
      GST_WARNING_OBJECT (core->object, "OMX_FreeBuffer (port: %d, omx_buffer = %p)", port->port_index, gomx_cmd->omx_buffer);
      if (gomx_cmd->omx_buffer) {
        OMX_FreeBuffer (core->omx_handle, port->port_index, gomx_cmd->omx_buffer);
      } else {
        GST_ERROR_OBJECT (core->object, "GSTOMX_COMMAND_FREE_BUFFER fail. omx_buffer is NULL");
      }
      break;

    default:
      break;
  }

  if(gomx_cmd) {
    g_free(gomx_cmd);
    gomx_cmd = NULL;
  }


leave:
  GST_LOG_OBJECT (core->object, "SendCommand leave");
  return;

pause:
  gst_task_pause(core->cmd.cmd_task);
  GST_WARNING_OBJECT (core->object, "SendCommand leave. paused command task.");
  return;
}

/*
 * OpenMAX IL callbacks.
 */

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE omx_handle,
    OMX_PTR app_data,
    OMX_EVENTTYPE event, OMX_U32 data_1, OMX_U32 data_2, OMX_PTR event_data)
{
  GOmxCore *core;
  GstOmxSendCmdQueue *gomx_cmd = NULL;

  core = (GOmxCore *) app_data;

  switch (event) {
    case OMX_EventCmdComplete:
    {
      OMX_COMMANDTYPE cmd;

      cmd = (OMX_COMMANDTYPE) data_1;

      GST_DEBUG_OBJECT (core->object, "OMX_EventCmdComplete: %d", cmd);

      switch (cmd) {
        case OMX_CommandStateSet:
          complete_change_state (core, data_2);
          break;
        case OMX_CommandFlush:
        { /* MODIFICATION */
          OMX_U32 port_index = (OMX_U32)data_2;
          GOmxPort *port = get_port (core, port_index);

          GST_WARNING_OBJECT (core->object, "we got OMX_CommandFlush complete event (port idx: %d)", port_index);

          port->enabled = TRUE;

          if (port->type == GOMX_PORT_OUTPUT && core->reconfiguring == GOMX_RECONF_STATE_PENDING) {
            GST_INFO_OBJECT (core->object, "we already have outport port settingchanged pending. do it now");
            PortSettingsChanged(core, port_index);
          } else {
            if (port->type == GOMX_PORT_OUTPUT) {
              OMX_BUFFERHEADERTYPE *omx_buffer;
              gint cnt = 0;
              GST_WARNING_OBJECT (core->object, "before async_queue_pop_forced. queue_len= %d", port->queue->length);
              while ((omx_buffer = async_queue_pop_forced (port->queue))) {
                omx_buffer->nFilledLen = 0;
                g_omx_port_release_buffer (port, omx_buffer);
                GST_INFO_OBJECT (core->object, "send FIllThisBuffer (%d)  omx_buffer = %p", cnt, omx_buffer);
                cnt++;
              }
              port->flushing = FALSE;
            }
            g_sem_up (core->flush_sem);
          }
          break;
        }
        case OMX_CommandPortDisable:
        { /* MODIFICATION */
          OMX_U32 port_index = (OMX_U32)data_2;
          GOmxPort *port = get_port (core, port_index);

          GST_WARNING_OBJECT (core->object, "we got OMX_CommandPortDisable complete event (port idx: %d)", port_index);

          port->enabled = FALSE;

          if (port->type == GOMX_PORT_OUTPUT && core->reconfiguring == GOMX_RECONF_STATE_START) {
            gomx_cmd = g_malloc(sizeof(GstOmxSendCmdQueue));
            gomx_cmd->type = GSTOMX_COMMAND_PORT_ENABLE;
            gomx_cmd->port = port->port_index;
            async_queue_push (core->cmd.cmd_queue, gomx_cmd);
          } else {
            g_sem_up (core->port_sem);
          }
          break;
        }
        case OMX_CommandPortEnable:
        { /* MODIFICATION */
          OMX_U32 port_index = (OMX_U32)data_2;
          GOmxPort *port = get_port (core, port_index);

          port->enabled = TRUE;

          GST_WARNING_OBJECT (core->object, "we got OMX_CommandPortEnable complete event (port idx: %d)", port_index);

          if (port->type == GOMX_PORT_OUTPUT && core->reconfiguring == GOMX_RECONF_STATE_START) {
            GST_INFO_OBJECT (core->object, "Reconfiguring is done. resume all port");
            if (core->omx_state == OMX_StateExecuting) {
              port_start_buffers (port);
            }
            /* First flush the queue and then enable it. This will ensure queue variables are reset */
            async_queue_flush (port->queue);
            g_omx_port_resume(port);

            core->reconfiguring = GOMX_RECONF_STATE_DONE;
            GST_WARNING_OBJECT (core->object, "after drc port enable, send signal DRC done to output_loop task.");
            g_cond_signal(core->drc_cond);
          } else {
            g_sem_up (core->port_sem);
          }
          break;
        }
        default:
          break;
      }
      break;
    }
    case OMX_EventBufferFlag:
    {
      GST_DEBUG_OBJECT (core->object, "OMX_EventBufferFlag");
      if (data_2 & OMX_BUFFERFLAG_EOS) {
        g_omx_core_set_done (core);
      }
      break;
    }
    case OMX_EventPortSettingsChanged:
    {
      GST_WARNING_OBJECT (core->object, "we got OMX_EventPortSettingsChanged. (in state = %d)", core->omx_state);
                /** @todo only on the relevant port. */
      /* MODIFICATION */
      if (data_2 == 0 || data_2 == OMX_IndexParamPortDefinition) {
        OMX_U32 port_index = (OMX_U32)data_1;
        GOmxPort *port = get_port (core, port_index);

        GST_WARNING_OBJECT (core->object, "start reconfiguring for output port");

        if (port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
          PortSettingsChanged(core, port_index);
        }
      } else if (data_2 == OMX_IndexConfigCommonOutputCrop) {
        core->crop_changed = TRUE;
        GST_WARNING_OBJECT (core->object, "we got only OMX_IndexConfigCommonOutputCrop");
      }

      if (core->settings_changed_cb) {
        core->settings_changed_cb (core);
      }
      break;
    }
    case OMX_EventError:
    {
      core->omx_error = data_1;
      GST_ERROR_OBJECT (core->object, "unrecoverable error: %s (0x%lx)",
          omx_error_to_str (data_1), data_1);
      /* component might leave us waiting for buffers, unblock */
      g_omx_core_flush_start (core);
      /* unlock wait_for_state */
      g_mutex_lock (core->omx_state_mutex);
      /* MODIFICATION: set to ignore condition signal to stop. */
      if ((core->component_vendor == GOMX_VENDOR_SLSI_EXYNOS ||
        core->component_vendor == GOMX_VENDOR_SLSI_SEC) &&
          core->omx_error == OMX_ErrorMFCInit) {
        GST_WARNING_OBJECT (core->object, "do not send g_cond_signal when MFC init fail. (%d)",
            core->omx_unrecover_err_cnt);
        if (core->omx_unrecover_err_cnt == 0) {
          if (core->post_gst_element_error == FALSE) {
            GST_ERROR_OBJECT (core->object, "post GST_ELEMENT_ERROR as Error from OpenMAX component");
            GST_ELEMENT_ERROR (core->object, STREAM, FAILED, (NULL), ("%s", "Error from OpenMAX component"));
            core->post_gst_element_error = TRUE;
          } else {
            GST_ERROR_OBJECT (core->object, "GST_ELEMENT_ERROR is already posted. skip this (Error from OpenMAX component)");
          }
        }
        core->omx_unrecover_err_cnt++;
      } else {
        g_cond_signal (core->omx_state_condition);
      }
      g_mutex_unlock (core->omx_state_mutex);
      if (core->omx_unrecover_err_cnt >= OMX_UNRECOVERABLE_ERROR_MAX_COUNT) {
        GST_WARNING_OBJECT (core->object, "got unrecoverable error too much. go to omx pause state");
        g_omx_core_pause(core);
        core->omx_unrecover_err_cnt = 0;
      }
      break;
    }
    default:
      break;
  }

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE omx_handle,
    OMX_PTR app_data, OMX_BUFFERHEADERTYPE * omx_buffer)
{
  GOmxCore *core;
  GOmxPort *port;
  private_data *pAppPrivate;

  core = (GOmxCore *) app_data;
  port = get_port (core, omx_buffer->nInputPortIndex);
  pAppPrivate = (private_data*)omx_buffer->pAppPrivate;

  GST_CAT_LOG_OBJECT (gstomx_util_debug, core->object, "omx_buffer = %p",
      omx_buffer);

  if (pAppPrivate) {
        if (core->input_log_count < MAX_DEBUG_FRAME_CNT) {
          GST_CAT_WARNING_OBJECT (gstomx_util_debug, core->object, "unref pAppPrivate. gst_buf= %p", pAppPrivate);
        } else {
          GST_CAT_LOG_OBJECT (gstomx_util_debug, core->object, "unref pAppPrivate. gst_buf= %p", pAppPrivate);
        }
        gst_buffer_unref ((GstBuffer *)pAppPrivate);

        pAppPrivate = NULL;
    }

  /* MODIFICATION: enc input buffer unref after EBD. */
  if (omx_buffer->nFlags == OMX_BUFFERFLAG_EOS && core->codec_type == GSTOMX_CODECTYPE_VIDEO_ENC) {
    if (omx_buffer->pBuffer != NULL) {
      GST_CAT_WARNING_OBJECT (gstomx_util_debug, core->object, "g_free eos omx_buffer->pBuffer. p = %p", omx_buffer->pBuffer);
      g_free(omx_buffer->pBuffer);
      omx_buffer->pBuffer = NULL;
    }
  }

  omx_buffer->nFlags = 0x00000000;
  got_buffer (core, port, omx_buffer);

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE omx_handle,
    OMX_PTR app_data, OMX_BUFFERHEADERTYPE * omx_buffer)
{
  GOmxCore *core;
  GOmxPort *port;
  OMX_PARAM_PORTDEFINITIONTYPE param;

  G_OMX_INIT_PARAM (param);

  core = (GOmxCore *) app_data;
  port = get_port (core, omx_buffer->nOutputPortIndex);

  if (core->output_log_count < MAX_DEBUG_FRAME_CNT) {
      GST_CAT_WARNING_OBJECT (gstomx_util_debug, core->object, "omx_buffer= %p  pBuf= %p nFill= %d state= %d",
      omx_buffer, omx_buffer->pBuffer, omx_buffer->nFilledLen, core->omx_state);
  }
  else
  {
      GST_CAT_LOG_OBJECT (gstomx_util_debug, core->object, "omx_buffer= %p  pBuf= %p nFill= %d state= %d",
      omx_buffer, omx_buffer->pBuffer, omx_buffer->nFilledLen, core->omx_state);
  }


  /* MODIFICATION: for DRC */
  if ((core->reconfiguring == GOMX_RECONF_STATE_START||
    core->reconfiguring == GOMX_RECONF_STATE_PENDING)&&
      (port->enabled == FALSE)) {
    GstOmxSendCmdQueue *gomx_cmd = NULL;
    SCMN_IMGB * buffer = NULL;

    buffer = (SCMN_IMGB*)omx_buffer->pBuffer;

    GST_WARNING_OBJECT (core->object, "this FBD is flush response. do not queue.");

    if (core->omx_state != OMX_StateExecuting) {
      GST_ERROR_OBJECT (core->object, "OMX_EventPortSettingsChanged but not executing. do not free buffer now.");
      goto queue_push;
    }

    /* send OMX_FreeBuffer */
    gomx_cmd = g_malloc(sizeof(GstOmxSendCmdQueue));
    gomx_cmd->type = GSTOMX_COMMAND_FREE_BUFFER;
    gomx_cmd->port = port->port_index;
    gomx_cmd->omx_buffer = omx_buffer;
    async_queue_push (core->cmd.cmd_queue, gomx_cmd);

    if (port->buffer_type == GOMX_BUFFER_GEM_VDEC_OUTPUT) {
      gint i = 0;
      for(i = 0; i < port->num_buffers ; i ++) {
        if (buffer->fd[0] == port->scmn_out[i].fd[0]) {
          GST_INFO_OBJECT(port->core->object, "tbm_bo_unref: bo[%d] Y plane: %p", i, port->bo[i].bo_y);
          tbm_bo_unref(port->bo[i].bo_y);
          port->bo[i].bo_y = NULL;

          GST_INFO_OBJECT(port->core->object, "tbm_bo_unref: bo[%d] UV plane: %p", i, port->bo[i].bo_uv);
          tbm_bo_unref(port->bo[i].bo_uv);
          port->bo[i].bo_uv = NULL;
          break;
        }
      }
    }
    goto exit;
  }


queue_push:
  got_buffer (core, port, omx_buffer);

exit:
  return OMX_ErrorNone;
}

static inline const char *
omx_state_to_str (OMX_STATETYPE omx_state)
{
  switch (omx_state) {
    case OMX_StateInvalid:
      return "invalid";
    case OMX_StateLoaded:
      return "loaded";
    case OMX_StateIdle:
      return "idle";
    case OMX_StateExecuting:
      return "executing";
    case OMX_StatePause:
      return "pause";
    case OMX_StateWaitForResources:
      return "wait for resources";
    default:
      return "unknown";
  }
}

static inline const char *
omx_error_to_str (OMX_ERRORTYPE omx_error)
{
  switch (omx_error) {
    case OMX_ErrorNone:
      return "None";

    case OMX_ErrorInsufficientResources:
      return
          "There were insufficient resources to perform the requested operation";

    case OMX_ErrorUndefined:
      return "The cause of the error could not be determined";

    case OMX_ErrorInvalidComponentName:
      return "The component name string was not valid";

    case OMX_ErrorComponentNotFound:
      return "No component with the specified name string was found";

    case OMX_ErrorInvalidComponent:
      return "The component specified did not have an entry point";

    case OMX_ErrorBadParameter:
      return "One or more parameters were not valid";

    case OMX_ErrorNotImplemented:
      return "The requested function is not implemented";

    case OMX_ErrorUnderflow:
      return "The buffer was emptied before the next buffer was ready";

    case OMX_ErrorOverflow:
      return "The buffer was not available when it was needed";

    case OMX_ErrorHardware:
      return "The hardware failed to respond as expected";

    case OMX_ErrorInvalidState:
      return "The component is in invalid state";

    case OMX_ErrorStreamCorrupt:
      return "Stream is found to be corrupt";

    case OMX_ErrorPortsNotCompatible:
      return "Ports being connected are not compatible";

    case OMX_ErrorResourcesLost:
      return "Resources allocated to an idle component have been lost";

    case OMX_ErrorNoMore:
      return "No more indices can be enumerated";

    case OMX_ErrorVersionMismatch:
      return "The component detected a version mismatch";

    case OMX_ErrorNotReady:
      return "The component is not ready to return data at this time";

    case OMX_ErrorTimeout:
      return "There was a timeout that occurred";

    case OMX_ErrorSameState:
      return
          "This error occurs when trying to transition into the state you are already in";

    case OMX_ErrorResourcesPreempted:
      return
          "Resources allocated to an executing or paused component have been preempted";

    case OMX_ErrorPortUnresponsiveDuringAllocation:
      return
          "Waited an unusually long time for the supplier to allocate buffers";

    case OMX_ErrorPortUnresponsiveDuringDeallocation:
      return
          "Waited an unusually long time for the supplier to de-allocate buffers";

    case OMX_ErrorPortUnresponsiveDuringStop:
      return
          "Waited an unusually long time for the non-supplier to return a buffer during stop";

    case OMX_ErrorIncorrectStateTransition:
      return "Attempting a state transition that is not allowed";

    case OMX_ErrorIncorrectStateOperation:
      return
          "Attempting a command that is not allowed during the present state";

    case OMX_ErrorUnsupportedSetting:
      return
          "The values encapsulated in the parameter or config structure are not supported";

    case OMX_ErrorUnsupportedIndex:
      return
          "The parameter or config indicated by the given index is not supported";

    case OMX_ErrorBadPortIndex:
      return "The port index supplied is incorrect";

    case OMX_ErrorPortUnpopulated:
      return
          "The port has lost one or more of its buffers and it thus unpopulated";

    case OMX_ErrorComponentSuspended:
      return "Component suspended due to temporary loss of resources";

    case OMX_ErrorDynamicResourcesUnavailable:
      return
          "Component suspended due to an inability to acquire dynamic resources";

    case OMX_ErrorMbErrorsInFrame:
      return "Frame generated macroblock error";

    case OMX_ErrorFormatNotDetected:
      return "Cannot parse or determine the format of an input stream";

    case OMX_ErrorContentPipeOpenFailed:
      return "The content open operation failed";

    case OMX_ErrorContentPipeCreationFailed:
      return "The content creation operation failed";

    case OMX_ErrorSeperateTablesUsed:
      return "Separate table information is being used";

    case OMX_ErrorTunnelingUnsupported:
      return "Tunneling is unsupported by the component";

    default:
      return "Unknown error";
  }
}
