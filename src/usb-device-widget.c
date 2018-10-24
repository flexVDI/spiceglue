/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2012 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#include <glib/gi18n.h>
#include "spice-client.h"
#include "usb-device-widget.h"
#include <spice-gtk/spice-util-priv.h>

/**
 * SECTION:usb-device-widget
 * @short_description: USB device selection glue
 * @title: Spice USB device selection glue
 * @section_id:
 * @see_also:
 * @stability: Stable
 * @include: usb-device-widget.h
 *
 * #SpiceUsbDeviceWidget is a Gobject that provides a simple C API to 
 * to select USB devices to redirect (or unredirect).
 */

/* ------------------------------------------------------------------ */
/* Prototypes for callbacks  */
static void device_added_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data);
static void device_removed_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data);
static void device_error_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, GError *err, gpointer user_data);
static void spice_usb_device_widget_update_status(gpointer user_data);

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_USB_DEVICE_WIDGET_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_USB_DEVICE_WIDGET, \
                                 SpiceUsbDeviceWidgetPrivate))

#define AS_IS (TRUE + FALSE + 1)

enum {
    PROP_0,
    PROP_SESSION
};


struct _SpiceUsbDeviceWidgetPrivate {
    SpiceSession *session;
    gchar *device_name_format_string;
    gchar *device_id_format_string;
    gchar *device_full_format_string;
    SpiceUsbDeviceManager *manager;

    /* Data accessed/modified by different threads (spice-glib / gui). */
    GSList *deviceList; // Contains UsbDeviceInfo*
    gchar *err_msg;

    gboolean isDeviceListChanged;
    gboolean isMsgChanged;

    GMutex deviceList_lock;
    GMutex err_msg_lock;

};

G_DEFINE_TYPE(SpiceUsbDeviceWidget, spice_usb_device_widget, G_TYPE_OBJECT);


static void spice_usb_device_widget_get_property(GObject     *gobject,
                                                 guint        prop_id,
                                                 GValue      *value,
                                                 GParamSpec  *pspec)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(gobject);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, priv->session);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_usb_device_widget_set_property(GObject       *gobject,
                                                 guint          prop_id,
                                                 const GValue  *value,
                                                 GParamSpec    *pspec)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(gobject);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        priv->session = g_value_dup_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static GObject *spice_usb_device_widget_constructor(
    GType gtype, guint n_properties, GObjectConstructParam *properties)
{
    GObject *obj;
    SpiceUsbDeviceWidget *self;
    SpiceUsbDeviceWidgetPrivate *priv;
    GPtrArray *devices = NULL;
    GError *err = NULL;
    int i;

    {
        /* Always chain up to the parent constructor */
        GObjectClass *parent_class;
        parent_class = G_OBJECT_CLASS(spice_usb_device_widget_parent_class);
        obj = parent_class->constructor(gtype, n_properties, properties);
    }
    
    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);

    self = SPICE_USB_DEVICE_WIDGET(obj);
    priv = self->priv;

    if (!priv->session)
        g_error("SpiceUsbDeviceWidget constructed without a session");
        
    priv->device_name_format_string =  _("%s %s");
    priv->device_id_format_string =  _("%.0s%.0s%.0s%d-%d");
    priv->device_full_format_string =  _("%s %s %s %d-%d"); // For logging


    priv->manager = spice_usb_device_manager_get(priv->session, &err);

    if (err) {
        g_warning(err->message);
        g_clear_error(&err);
        return obj;
    }
    g_mutex_init(&priv->deviceList_lock);
    g_mutex_init(&priv->err_msg_lock);

    /*  Added to the local machine. 
     *  Different from "connected" which means redirected. */
    g_signal_connect(priv->manager, "device-added",
                     G_CALLBACK(device_added_cb), self);
    g_signal_connect(priv->manager, "device-removed",
                     G_CALLBACK(device_removed_cb), self);
    g_signal_connect(priv->manager, "device-error",
                     G_CALLBACK(device_error_cb), self);

    priv->deviceList = NULL;
    priv->isDeviceListChanged = TRUE;
    devices = spice_usb_device_manager_get_devices(priv->manager);
    if (!devices)
        goto end;

    for (i = 0; i < devices->len; i++) {
        g_info("connecting device %p", g_ptr_array_index(devices, i));
        device_added_cb(NULL, g_ptr_array_index(devices, i), self);
    }

    g_ptr_array_unref(devices);

end:
    spice_usb_device_widget_update_status(self);

    return obj;
}

static void spice_usb_device_widget_finalize(GObject *object)
{
    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(object);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    
    g_mutex_lock(&priv->deviceList_lock);
    if (priv->deviceList) 
        g_slist_free(priv->deviceList);
    g_mutex_unlock(&priv->deviceList_lock);
    g_mutex_clear(&priv->deviceList_lock);
    
    g_mutex_lock(&priv->err_msg_lock);
    if (priv->err_msg != NULL) {
        g_free(priv->err_msg);
    }
    g_mutex_unlock(&priv->err_msg_lock);
    g_mutex_clear(&priv->err_msg_lock);
    
        
    if (priv->manager) {
        g_signal_handlers_disconnect_by_func(priv->manager,
                                             device_added_cb, self);
        g_signal_handlers_disconnect_by_func(priv->manager,
                                             device_removed_cb, self);
        g_signal_handlers_disconnect_by_func(priv->manager,
                                             device_error_cb, self);
    }
    

    g_object_unref(priv->session);

    if (G_OBJECT_CLASS(spice_usb_device_widget_parent_class)->finalize)
        G_OBJECT_CLASS(spice_usb_device_widget_parent_class)->finalize(object);
}

static void spice_usb_device_widget_class_init(
    SpiceUsbDeviceWidgetClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GParamSpec *pspec;

    g_type_class_add_private (klass, sizeof (SpiceUsbDeviceWidgetPrivate));

    gobject_class->constructor  = spice_usb_device_widget_constructor;
    gobject_class->finalize     = spice_usb_device_widget_finalize;
    gobject_class->get_property = spice_usb_device_widget_get_property;
    gobject_class->set_property = spice_usb_device_widget_set_property;

    /**
     * SpiceUsbDeviceWidget:session:
     *
     * #SpiceSession this #SpiceUsbDeviceWidget is associated with
     *
     **/
    pspec = g_param_spec_object("session",
                                "Session",
                                "SpiceSession",
                                SPICE_TYPE_SESSION,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_SESSION, pspec);

}

static void spice_usb_device_widget_init(SpiceUsbDeviceWidget *self)
{
    self->priv = SPICE_USB_DEVICE_WIDGET_GET_PRIVATE(self);
}

/* ------------------------------------------------------------------ */
/* public api                                                         */

/**
 * spice_usb_device_widget_new:
 * @session: #SpiceSession for which to widget will control USB redirection
 *
 * Returns: a new #SpiceUsbDeviceWidget instance
 */
GObject *spice_usb_device_widget_new(SpiceSession    *session)
{
    return g_object_new(SPICE_TYPE_USB_DEVICE_WIDGET,
                        "session", session,
                        NULL);
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */

/*  Adds text to the err_msg (if not already there).
 */
static void addErrorMessage(SpiceUsbDeviceWidget *self, char* newMessage) {

    g_debug(" %s:%d:%s() %s", __FILE__, __LINE__, __func__, newMessage);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    g_mutex_lock(&priv->err_msg_lock);
    /* If we cannot redirect this device, append the error message to
       err_msg, but only if it is *not* already there! */

    priv->isMsgChanged = TRUE;
    if (priv->err_msg) {
        if (!strstr(priv->err_msg, newMessage)) {
            gchar *old_err_msg = priv->err_msg;

            priv->err_msg = g_strdup_printf("%s\n%s", priv->err_msg,
                                            newMessage);
            g_free(old_err_msg);
        }
    } else {
        priv->err_msg = g_strdup(newMessage);
    }
    g_mutex_unlock(&priv->err_msg_lock);
}

/* Checks that the device in the deviInfo can be redirected.
 * If one device is not redirectable:
 *  - Adds an error message
 *  - disables/enables the widget accordingly
 * widget: el check que contiene el property con el device.
 * user_data = self: el spice-usb-widget: (container)
*/
static void check_can_redirect(SpiceUsbDeviceWidget *self, UsbDeviceInfo *devInfo)
{
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    SpiceUsbDevice *device = devInfo->device;
    gboolean can_redirect;
    GError *err = NULL;

    can_redirect = spice_usb_device_manager_can_redirect_device(priv->manager,
                                                                device, &err);
                                                                
    if (devInfo->isEnabled!= can_redirect) {
        g_debug(" %s:%d:%s() ", __FILE__, __LINE__, __func__);
        SPICE_DEBUG("USB: NOT changing program state. check_can_redirect changed for %s, id %s, enabled: %d, can_redir: %d", 
                    devInfo->name, devInfo->id, devInfo->isEnabled, can_redirect);
        /*
        FIXME: First time this is called, it says VM is not configured for redirection. 
        But it obviously is, as it redirects usbs if we ignore this value...
        We should correct the function / call it before / something so that we don't ignore
        this value always.
         
        SPICE_DEBUG("USB: check_can_redirect changed for %s, id %s, enabled: %d, can_redir: %d", 
                    devInfo->name, devInfo->id, devInfo->isEnabled, can_redirect);

        devInfo->isEnabled= can_redirect;*/
        priv->isDeviceListChanged = TRUE;
    }

    if (!can_redirect) {
        addErrorMessage(self, err->message);
    }
    
    g_clear_error(&err);
}

/* Retrieves the error message for a client program and resets it to "". */
void spice_usb_device_widget_get_error_msg(SpiceUsbDeviceWidget* self, 
        char* msg) {

    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    if (priv->err_msg == NULL) return;
    
    g_mutex_lock(&priv->err_msg_lock);
    strncpy(msg, priv->err_msg, MAX_USB_ERR_MSG_SIZE);
    g_free(priv->err_msg);
    priv->err_msg = NULL;
    priv->isMsgChanged = FALSE;
    g_mutex_unlock(&priv->err_msg_lock);
}

/*  
 * Checks that:
 * - every listed device can be redirected
 * - there are >0 devices
 * And updates the error message and Device List (disabling devices) accordingly.
 */
static void spice_usb_device_widget_update_status(gpointer user_data)
{

    SpiceUsbDeviceWidget *self = (SpiceUsbDeviceWidget *)user_data;
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    g_mutex_lock(&priv->deviceList_lock);
    GSList *iterator = NULL;
    for (iterator = priv->deviceList; iterator; iterator = iterator->next) {
        UsbDeviceInfo *d = iterator->data;
        check_can_redirect(self, d);
    }
    g_mutex_unlock(&priv->deviceList_lock);
}

/* Update the isShared and isOpPending status of the corresponding device.
 */
static void flagStatusPerDevice (SpiceUsbDeviceWidget* self, 
        SpiceUsbDevice* device, guint isShared, guint isOpPending) {

    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    g_mutex_lock(&priv->deviceList_lock);
    GSList *iterator = NULL;
    for (iterator = priv->deviceList; iterator; iterator = iterator->next) {
        UsbDeviceInfo* info = (UsbDeviceInfo*)iterator->data;
        if (info->device == device) {
            if (isShared != AS_IS)
                info->isShared= isShared;
            if (isOpPending != AS_IS)
                info->isOpPending = isOpPending;
            SPICE_DEBUG("USB: device %s, id %s is set as shared %d", 
                info->name, info->id, info->isShared);
        }
    }
    priv->isDeviceListChanged = TRUE;
    g_mutex_unlock(&priv->deviceList_lock);
}

typedef struct _ {
    SpiceUsbDevice *device; // The device that was asked to be shared / unshared
    SpiceUsbDeviceWidget *self;
} connect_cb_data;

/* Called when the usb redirection completes */
static void connect_cb(GObject *gobject, GAsyncResult *res, gpointer user_data)
{
    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    connect_cb_data *data = user_data;
    SpiceUsbDeviceWidget *self = data->self;
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    SpiceUsbDevice *device;
    GError *err = NULL;
    gchar *desc;

    spice_usb_device_manager_connect_device_finish(manager, res, &err);
    device = data->device;
    desc = spice_usb_device_get_description(device,
                                                priv->device_full_format_string);

    if (err) {
        desc = spice_usb_device_get_description(device,
                                                priv->device_full_format_string);
        g_prefix_error(&err, "Could not redirect %s: ", desc);
        g_free(desc);

        addErrorMessage(self, err->message);
        //g_signal_emit(self, signals[CONNECT_FAILED], 0, device, err);
        g_error_free(err);

        spice_usb_device_widget_update_status(self);
        flagStatusPerDevice(self, device, AS_IS, FALSE);
    } else {
        flagStatusPerDevice(self, device, TRUE, FALSE);
    }   
    
    priv->isDeviceListChanged = TRUE;
    g_object_unref(data->self);
    g_free(data);
}


void spice_usb_device_widget_share(SpiceUsbDeviceWidget* self, 
        SpiceUsbDevice *device)
{
    g_debug(" %s:%d:%s(%p)", __FILE__, __LINE__, __func__, device);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    
    connect_cb_data *data = g_new(connect_cb_data, 1);
    data->device = device;
    data->self  = g_object_ref(self);

    spice_usb_device_manager_connect_device_async(priv->manager,
                                                  device,
                                                  NULL,
                                                  connect_cb,
                                                  data);
                                                  
    /* Flag the deviceInfo as isOpPending for activity.
     * If we try to disconnect it before it finishes the connection, the program can
     * hang/crash/leave the usb blinking forever... */
    GSList *iterator;
    
    g_mutex_lock(&priv->deviceList_lock);

    for (iterator = priv->deviceList; iterator; iterator = iterator->next) {
        UsbDeviceInfo* info = (UsbDeviceInfo*)iterator->data;
        SPICE_DEBUG("Pending: Comparing to %s: %s", info->name, info->id);
        if (info->device == device) {
            info->isOpPending = TRUE;
            SPICE_DEBUG("Pending: flagging as pending %s: %s", info->name, info->id);
        }
    }
    priv->isDeviceListChanged = TRUE;
    g_mutex_unlock(&priv->deviceList_lock);

    spice_usb_device_widget_update_status(self);
}

/* Private function. Called from mainloop */
static gboolean spice_usb_device_widget_unshare1(gpointer d)
{
    connect_cb_data *data = (connect_cb_data *)d;
    
    SpiceUsbDeviceWidget* self = data->self;
    SpiceUsbDevice *device = data->device;
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    
    spice_usb_device_manager_disconnect_device(priv->manager, device);

    flagStatusPerDevice(self, device, FALSE, FALSE);
    spice_usb_device_widget_update_status(self);
    g_object_unref(self);
    g_free(data);
    return FALSE;
}

/* Public function callable from client program thread */ 
void spice_usb_device_widget_unshare(SpiceUsbDeviceWidget* self, 
        SpiceUsbDevice *device)
{
    connect_cb_data *data = g_new(connect_cb_data, 1);
    data->self  = g_object_ref(self);
    data->device  = device;
    g_timeout_add_full(G_PRIORITY_HIGH, 0,
                       spice_usb_device_widget_unshare1,
                       data, NULL);

}

gboolean spice_usb_device_widget_is_changed(SpiceUsbDeviceWidget* self) {

    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    
    return priv->isDeviceListChanged;
}

/* Creates a new copy of the list of usb devices, and returns it to the caller
*/
GSList *spice_usb_device_widget_get_devices(SpiceUsbDeviceWidget* self) {

    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);

    SpiceUsbDeviceWidgetPrivate *priv = self->priv; 
    
    GSList *listCopy = NULL;
    GSList *iterator = NULL;
    
    g_mutex_lock(&priv->deviceList_lock);
    for (iterator = priv->deviceList; iterator; iterator = iterator->next) {
        UsbDeviceInfo *d = iterator->data;
        UsbDeviceInfo *devCopy = g_new (UsbDeviceInfo, 1);
        strncpy(devCopy->name, d->name, MAX_USB_DEVICE_NAME_SIZE);
        strncpy(devCopy->id, d->id, MAX_USB_DEVICE_ID_SIZE);
        devCopy->isShared = d->isShared;
        devCopy->device = d->device;
        devCopy->isEnabled = d->isEnabled;
        devCopy->isOpPending = d->isOpPending;
        SPICE_DEBUG("Returning copy of USB Device; id: %s, *dev %p, shared= %d, enabled: %d, desc: %s", 
            devCopy->id, devCopy->device, devCopy->isShared, devCopy->isEnabled, devCopy->name);
        listCopy = g_slist_append(listCopy, devCopy);
    }
    
    priv->isDeviceListChanged = FALSE;
    g_mutex_unlock(&priv->deviceList_lock);
    
    return listCopy;
}

/* Called when a new usb is connected, and initially for all the devices connected
 */
static void device_added_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data)
{
    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);
    
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(user_data);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    
    UsbDeviceInfo *deviceInfo = g_new(UsbDeviceInfo, 1);
    
    deviceInfo->device = device;

    gchar *desc;
    desc = spice_usb_device_get_description(device,
                                            priv->device_name_format_string);
    strncpy(deviceInfo->name, desc, MAX_USB_DEVICE_NAME_SIZE);
    g_free(desc);
    
    gchar *id;
    id = spice_usb_device_get_description(device,
                                            priv->device_id_format_string);
    strncpy(deviceInfo->id, id, MAX_USB_DEVICE_ID_SIZE);
    g_free(id);
    
    gboolean shared= spice_usb_device_manager_is_device_connected(
            priv->manager, device);
    deviceInfo->isShared = shared;
    deviceInfo->isEnabled = TRUE;
    deviceInfo->isOpPending = FALSE;

    // Logging just in case something changes
    if (shared) {
        SPICE_DEBUG("USB: Just added device already connected; id: %s shared: %d", 
        deviceInfo->id, shared);
    }
    SPICE_DEBUG("New USB Device; id: %s, *dev %p, shared= %d, enabled: %d, desc: %s", 
        deviceInfo->id, device, deviceInfo->isShared, deviceInfo->isEnabled, deviceInfo->name);
    g_mutex_lock(&priv->deviceList_lock);
    priv->deviceList = g_slist_append(priv->deviceList, deviceInfo);
    priv->isDeviceListChanged = TRUE;
    g_mutex_unlock(&priv->deviceList_lock);
}


/* Removes the deviceInfo associated to the removed device from the deviceList
 */
 static void device_removed_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data)
{
    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(user_data);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    GSList *iterator;
    
    g_mutex_lock(&priv->deviceList_lock);

    for (iterator = priv->deviceList; iterator; iterator = iterator->next) {
        UsbDeviceInfo* info = (UsbDeviceInfo*)iterator->data;
        SPICE_DEBUG("REMOVE: Comparing to %s: %s", info->name, info->id);
        if (info->device == device) {
            priv->deviceList = g_slist_remove(priv->deviceList, info);
            SPICE_DEBUG("REMOVE: gonna free %s: %s", info->name, info->id);
            g_free(info);
        }
    }
    priv->isDeviceListChanged = TRUE;
    g_mutex_unlock(&priv->deviceList_lock);
}


/* In case of error, mark all the deviceds as not redirected 
   y priv->isDeviceListChanged = TRUE;
   Spicy sets al buttons to "inactive" when this is triggered
 */
static void device_error_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, GError *err, gpointer user_data)
{
    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(user_data);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    gchar *full_desc;
    
    if (err) {
        full_desc = spice_usb_device_get_description(device,
                                                priv->device_full_format_string);
        g_prefix_error(&err, "Error redirecting usb device %s ", full_desc);
        g_free(full_desc);
        
        g_warning(err->message);
        addErrorMessage(self, err->message);
        g_error_free(err);
    } else {
        g_warning("Device_error. No additional data available.");
        addErrorMessage(self, "Device_error. No additional data available.");
    }

    spice_usb_device_widget_update_status(self);
}

gboolean spice_usb_device_widget_is_msg_changed(SpiceUsbDeviceWidget* self) {
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    
    return priv->isMsgChanged;
}
