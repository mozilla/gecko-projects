/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"

#include "nsArrayUtils.h"
#include "nsClipboard.h"
#include "nsClipboardWayland.h"
#include "nsIStorageStream.h"
#include "nsIBinaryOutputStream.h"
#include "nsSupportsPrimitives.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsPrimitiveHelpers.h"
#include "nsIServiceManager.h"
#include "nsImageToPixbuf.h"
#include "nsStringStream.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"

#include "imgIContainer.h"

#include <gtk/gtk.h>
#include <poll.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <errno.h>

void
nsRetrievalContextWayland::ResetMIMETypeList(void)
{
  mTargetMIMETypes.Clear();
}

void
nsRetrievalContextWayland::AddMIMEType(const char *aMimeType)
{
  GdkAtom atom = gdk_atom_intern(aMimeType, FALSE);
  mTargetMIMETypes.AppendElement(atom);
}

void
nsRetrievalContextWayland::SetDataOffer(wl_data_offer *aDataOffer)
{
    if(mDataOffer) {
        wl_data_offer_destroy(mDataOffer);
    }
    mDataOffer = aDataOffer;
}

static void
data_device_selection (void                  *data,
                       struct wl_data_device *wl_data_device,
                       struct wl_data_offer  *offer)
{
    nsRetrievalContextWayland *context =
        static_cast<nsRetrievalContextWayland*>(data);
    context->SetDataOffer(offer);
}

static void
data_offer_offer (void                 *data,
                  struct wl_data_offer *wl_data_offer,
                  const char           *type)
{
  nsRetrievalContextWayland *context =
      static_cast<nsRetrievalContextWayland*>(data);
  context->AddMIMEType(type);
}

static void
data_offer_source_actions(void *data,
                          struct wl_data_offer *wl_data_offer,
                          uint32_t source_actions)
{
}

static void
data_offer_action(void *data,
                  struct wl_data_offer *wl_data_offer,
                  uint32_t dnd_action)
{
}

static const struct wl_data_offer_listener data_offer_listener = {
    data_offer_offer,
    data_offer_source_actions,
    data_offer_action
};

static void
data_device_data_offer (void                  *data,
                        struct wl_data_device *data_device,
                        struct wl_data_offer  *offer)
{
    nsRetrievalContextWayland *context =
        static_cast<nsRetrievalContextWayland*>(data);

    // We have a new fresh clipboard content
    context->ResetMIMETypeList();
    wl_data_offer_add_listener (offer, &data_offer_listener, data);
}

static void
data_device_enter (void                  *data,
                   struct wl_data_device *data_device,
                   uint32_t               time,
                   struct wl_surface     *surface,
                   int32_t                x,
                   int32_t                y,
                   struct wl_data_offer  *offer)
{
}

static void
data_device_leave (void                  *data,
                   struct wl_data_device *data_device)
{
}

static void
data_device_motion (void                  *data,
                    struct wl_data_device *data_device,
                    uint32_t               time,
                    int32_t                x,
                    int32_t                y)
{
}

static void
data_device_drop (void                  *data,
                  struct wl_data_device *data_device)
{
}

static const struct wl_data_device_listener data_device_listener = {
    data_device_data_offer,
    data_device_enter,
    data_device_leave,
    data_device_motion,
    data_device_drop,
    data_device_selection
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                       uint32_t format, int fd, uint32_t size)
{
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface,
                      struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
    // We lost focus so our clipboard data are outdated
    nsRetrievalContextWayland *context =
        static_cast<nsRetrievalContextWayland*>(data);

    context->ResetMIMETypeList();
    context->SetDataOffer(nullptr);
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                    uint32_t serial, uint32_t time, uint32_t key,
                    uint32_t state)
{
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked,
                          uint32_t group)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
};

void
nsRetrievalContextWayland::ConfigureKeyboard(wl_seat_capability caps)
{
  // ConfigureKeyboard() is called when wl_seat configuration is changed.
  // We look for the keyboard only, get it when is't available and release it
  // when it's lost (we don't have focus for instance).
  if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
      mKeyboard = wl_seat_get_keyboard(mSeat);
      wl_keyboard_add_listener(mKeyboard, &keyboard_listener, this);
  } else if (mKeyboard && !(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
      wl_keyboard_destroy(mKeyboard);
      mKeyboard = nullptr;
  }
}

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
                         unsigned int caps)
{
    nsRetrievalContextWayland *context =
        static_cast<nsRetrievalContextWayland*>(data);
    context->ConfigureKeyboard((wl_seat_capability)caps);
}

static const struct wl_seat_listener seat_listener = {
      seat_handle_capabilities,
};

void
nsRetrievalContextWayland::InitDataDeviceManager(wl_registry *registry,
                                                 uint32_t id,
                                                 uint32_t version)
{
  int data_device_manager_version = MIN (version, 3);
  mDataDeviceManager = (wl_data_device_manager *)wl_registry_bind(registry, id,
      &wl_data_device_manager_interface, data_device_manager_version);
}

void nsRetrievalContextWayland::InitSeat(wl_registry *registry,
                                         uint32_t id, uint32_t version,
                                         void *data)
{
  mSeat = (wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 1);
  wl_seat_add_listener(mSeat, &seat_listener, data);
}

static void
gdk_registry_handle_global(void               *data,
                           struct wl_registry *registry,
                           uint32_t            id,
                           const char         *interface,
                           uint32_t            version)
{
  nsRetrievalContextWayland *context =
      static_cast<nsRetrievalContextWayland*>(data);

  if (strcmp (interface, "wl_data_device_manager") == 0) {
    context->InitDataDeviceManager(registry, id, version);
  } else if (strcmp(interface, "wl_seat") == 0) {
    context->InitSeat(registry, id, version, data);
  }
}

static void
gdk_registry_handle_global_remove(void               *data,
                                 struct wl_registry *registry,
                                 uint32_t            id)
{
}

static const struct wl_registry_listener clipboard_registry_listener = {
    gdk_registry_handle_global,
    gdk_registry_handle_global_remove
};

nsRetrievalContextWayland::nsRetrievalContextWayland(void)
  : mInitialized(false)
  , mSeat(nullptr)
  , mDataDeviceManager(nullptr)
  , mDataOffer(nullptr)
  , mKeyboard(nullptr)
  , mClipboardRequestNumber(0)
  , mClipboardData(nullptr)
  , mClipboardDataLength(0)
{
    const gchar* charset;
    g_get_charset(&charset);
    mTextPlainLocale = g_strdup_printf("text/plain;charset=%s", charset);

    // Available as of GTK 3.8+
    static auto sGdkWaylandDisplayGetWlDisplay =
        (wl_display *(*)(GdkDisplay *))
        dlsym(RTLD_DEFAULT, "gdk_wayland_display_get_wl_display");

    mDisplay = sGdkWaylandDisplayGetWlDisplay(gdk_display_get_default());
    wl_registry_add_listener(wl_display_get_registry(mDisplay),
                             &clipboard_registry_listener, this);
    // Call wl_display_roundtrip() twice to make sure all
    // callbacks are processed.
    wl_display_roundtrip(mDisplay);
    wl_display_roundtrip(mDisplay);

    // mSeat/mDataDeviceManager should be set now by
    // gdk_registry_handle_global() as a response to
    // wl_registry_add_listener() call.
    if (!mDataDeviceManager || !mSeat)
        return;

    wl_data_device *dataDevice =
        wl_data_device_manager_get_data_device(mDataDeviceManager, mSeat);
    wl_data_device_add_listener(dataDevice, &data_device_listener, this);
    // We have to call wl_display_roundtrip() twice otherwise data_offer_listener
    // may not be processed because it's called from data_device_data_offer
    // callback.
    wl_display_roundtrip(mDisplay);
    wl_display_roundtrip(mDisplay);

    mInitialized = true;
}

nsRetrievalContextWayland::~nsRetrievalContextWayland(void)
{
    g_free(mTextPlainLocale);
}

GdkAtom*
nsRetrievalContextWayland::GetTargets(int32_t aWhichClipboard,
                                      int* aTargetNum)
{
    int length = mTargetMIMETypes.Length();
    if (!length) {
        *aTargetNum = 0;
        return nullptr;
    }

    GdkAtom* targetList = reinterpret_cast<GdkAtom*>(
        g_malloc(sizeof(GdkAtom)*length));
    for (int32_t j = 0; j < length; j++) {
        targetList[j] = mTargetMIMETypes[j];
    }

    *aTargetNum = length;
    return targetList;
}

struct FastTrackClipboard
{
    FastTrackClipboard(int aClipboardRequestNumber,
                       nsRetrievalContextWayland* aRetrievalContex)
    : mClipboardRequestNumber(aClipboardRequestNumber)
    , mRetrievalContex(aRetrievalContex)
    {}

    int                        mClipboardRequestNumber;
    nsRetrievalContextWayland* mRetrievalContex;
};

static void
wayland_clipboard_contents_received(GtkClipboard     *clipboard,
                                    GtkSelectionData *selection_data,
                                    gpointer          data)
{
    FastTrackClipboard* fastTrack = static_cast<FastTrackClipboard*>(data);
    fastTrack->mRetrievalContex->TransferFastTrackClipboard(
        fastTrack->mClipboardRequestNumber, selection_data);
    delete fastTrack;
}

void
nsRetrievalContextWayland::TransferFastTrackClipboard(
    int aClipboardRequestNumber, GtkSelectionData *aSelectionData)
{
    if (mClipboardRequestNumber == aClipboardRequestNumber) {
        mClipboardDataLength = gtk_selection_data_get_length(aSelectionData);
        if (mClipboardDataLength > 0) {
            mClipboardData = reinterpret_cast<char*>(
                g_malloc(sizeof(char)*mClipboardDataLength));
            memcpy(mClipboardData, gtk_selection_data_get_data(aSelectionData),
                   sizeof(char)*mClipboardDataLength);
        }
    } else {
        NS_WARNING("Received obsoleted clipboard data!");
    }
}

const char*
nsRetrievalContextWayland::GetClipboardData(const char* aMimeType,
                                            int32_t aWhichClipboard,
                                            uint32_t* aContentLength)
{
    NS_ASSERTION(mClipboardData == nullptr && mClipboardDataLength == 0,
                 "Looks like we're leaking clipboard data here!");

    /* If actual clipboard data is owned by us we don't need to go
     * through Wayland but we ask Gtk+ to directly call data
     * getter callback nsClipboard::SelectionGetEvent().
     * see gtk_selection_convert() at gtk+/gtkselection.c.
     */
    GdkAtom selection = GetSelectionAtom(aWhichClipboard);
    if (gdk_selection_owner_get(selection)) {
        mClipboardRequestNumber++;
        gtk_clipboard_request_contents(gtk_clipboard_get(selection),
            gdk_atom_intern(aMimeType, FALSE),
            wayland_clipboard_contents_received,
            new FastTrackClipboard(mClipboardRequestNumber, this));
    } else {
        /* TODO: We need to implement GDK_SELECTION_PRIMARY (X11 text selection)
         * for Wayland backend.
         */
        if (selection == GDK_SELECTION_PRIMARY)
             return nullptr;

        NS_ASSERTION(mDataOffer, "Requested data without valid data offer!");

        if (!mDataOffer) {
            // TODO
            // Something went wrong. We're requested to provide clipboard data
            // but we haven't got any from wayland. Looks like rhbz#1455915.
            return nullptr;
        }

        int pipe_fd[2];
        if (pipe(pipe_fd) == -1)
            return nullptr;

        wl_data_offer_receive(mDataOffer, aMimeType, pipe_fd[1]);
        close(pipe_fd[1]);
        wl_display_flush(mDisplay);

        struct pollfd fds;
        fds.fd = pipe_fd[0];
        fds.events = POLLIN;

        // Choose some reasonable timeout here
        int ret = poll(&fds, 1, kClipboardTimeout / 1000);
        if (!ret || ret == -1) {
            close(pipe_fd[0]);
            return nullptr;
        }

        GIOChannel *channel = g_io_channel_unix_new(pipe_fd[0]);
        GError* error = nullptr;

        g_io_channel_set_encoding(channel, nullptr, &error);
        if (!error) {
            gsize length = 0;
            g_io_channel_read_to_end(channel, &mClipboardData, &length, &error);
            mClipboardDataLength = length;
        }

        if (error) {
            NS_WARNING(
                nsPrintfCString("Unexpected error when reading clipboard data: %s",
                                error->message).get());
            g_error_free(error);
        }

        g_io_channel_unref(channel);
        close(pipe_fd[0]);

        // We don't have valid clipboard data although
        // g_io_channel_read_to_end() allocated mClipboardData for us.
        // Release it now and return nullptr to indicate
        // we don't have reqested data flavour.
        if (mClipboardData && mClipboardDataLength == 0) {
            ReleaseClipboardData(mClipboardData);
        }
    }

    *aContentLength = mClipboardDataLength;
    return reinterpret_cast<const char*>(mClipboardData);
}

void nsRetrievalContextWayland::ReleaseClipboardData(const char* aClipboardData)
{
    NS_ASSERTION(aClipboardData == mClipboardData,
        "Releasing unknown clipboard data!");
    g_free((void*)aClipboardData);

    mClipboardData = nullptr;
    mClipboardDataLength = 0;
}
