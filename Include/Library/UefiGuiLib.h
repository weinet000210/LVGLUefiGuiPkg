/** @file
  UefiGuiLib - public API of the UefiGui framework.

  These are the only UefiGui functions a typical application calls; it never
  touches GOP, input protocols or the LVGL display/indev setup directly.

  What initializes LVGL: UefiGuiInitialize() calls lv_init(), installs the
  tick, log, memory and assert hooks, creates the LVGL display (partial
  rendering, XRGB8888 draw buffers) and connects it to UefiGuiDisplayLib.
  UefiGuiShutdown() deletes the display and calls lv_deinit().
  What the application calls: everything else in this header, plus LVGL
  widget APIs (lv_label_create(), lv_button_create(), ...) on the active
  screen.

  Input (Phases 3 and 4): UefiGuiInitialize() creates an LVGL keypad input
  device for the keyboard and a pointer input device when a mouse or an
  absolute pointer exists. A default lv_group is created and set with
  lv_group_set_default(), so widgets that accept focus are added to it as
  they are created and Tab / Shift+Tab move the focus.

  Key mapping (UefiGuiKeyToLvgl): arrows -> LV_KEY_UP/DOWN/LEFT/RIGHT,
  Enter -> LV_KEY_ENTER, Esc -> LV_KEY_ESC, Tab -> LV_KEY_NEXT,
  Shift+Tab -> LV_KEY_PREV, Backspace -> LV_KEY_BACKSPACE,
  Delete -> LV_KEY_DEL, Home -> LV_KEY_HOME, End -> LV_KEY_END, printable
  characters -> their UTF-8 code. Page Up / Page Down have no LVGL key: they
  scroll the focused object (or its nearest scrollable parent) by one page.
  Ctrl / Alt combinations (Ctrl+A, C, V, S, O, F, ...) are never typed into
  widgets: they go to the handler set with UefiGuiSetKeyHandler(), which
  also sees every other key first and may consume it.

  Right button: a click (press and release) sends the event code returned by
  UefiGuiGetRightClickEventCode() to the object under the cursor, with the
  lv_point_t position as the event parameter. Wheel: scrolls the scrollable
  object under the cursor.

  Call order:
    UefiGuiConfigInitDefaults (&Config);   // application, optional overrides
    UefiGuiInitialize (&Config);           // initializes LVGL, display, input
    ... build widgets with LVGL APIs ...
    UefiGuiRun ();                         // returns after UefiGuiRequestExit()
    UefiGuiShutdown ();                    // releases everything, deinits LVGL

  Applications with their own loop call UefiGuiProcessEvents() repeatedly
  instead of UefiGuiRun().

  Event loop (Phase 5): single threaded. Each UefiGuiProcessEvents() call
    1. runs callbacks posted with UefiGuiPostCallback(),
    2. reads queued keyboard / pointer input and hands it to LVGL,
    3. runs lv_timer_handler(),
    4. sleeps in gBS->WaitForEvent() until the earliest of: the next LVGL
       timer (clamped to Timer.MinWaitMs .. Timer.MaxWaitMs), a keyboard or
       pointer wait event, UefiGuiRequestExit(), UefiGuiPostCallback(), or
       an application event added with UefiGuiRegisterEvent(),
    5. runs the handlers of signaled application events.
  The LVGL input devices run in event mode and the display refresh timer is
  paused while nothing is invalidated, so an idle screen wakes the loop only
  every Timer.MaxWaitMs. Nothing is polled in a busy loop.

  TPL rules: every function in this header must be called from the
  application's main execution context at TPL_APPLICATION, and LVGL APIs may
  only be called from that same context, EXCEPT UefiGuiRequestExit() and
  UefiGuiPostCallback(), which may also be called from UEFI event
  notification functions at up to TPL_NOTIFY. A notification function must
  never touch LVGL: it posts a callback, and the callback runs in the GUI
  context during the next loop iteration.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_GUI_LIB_H_
#define UEFI_GUI_LIB_H_

#include <UefiGui/UefiGuiTypes.h>
#include <UefiGui/UefiGuiConfig.h>
#include <UefiGui/UefiGuiStatus.h>

/**
  Fill Config with the documented defaults.
**/
VOID
EFIAPI
UefiGuiConfigInitDefaults (
  OUT UEFI_GUI_CONFIG  *Config
  );

/**
  Initialize LVGL and all UefiGui backends.

  @param[in]  Config   Options, or NULL for defaults.

  @retval EFI_SUCCESS            Ready. Missing input devices are tolerated;
                                 see UefiGuiGetLastError().
  @retval EFI_ALREADY_STARTED    Already initialized.
  @retval EFI_INCOMPATIBLE_VERSION  Config->Revision is not supported.
  @retval EFI_NOT_FOUND          No GOP.
  @retval other                  Backend failure; nothing is left allocated.
**/
EFI_STATUS
EFIAPI
UefiGuiInitialize (
  IN UEFI_GUI_CONFIG  *Config  OPTIONAL
  );

/**
  Run the event loop until UefiGuiRequestExit() is called.

  @retval EFI_SUCCESS       An exit was requested (the request is consumed,
                            so UefiGuiRun() can be called again).
  @retval EFI_NOT_STARTED   Not initialized.
  @retval EFI_ACCESS_DENIED Called from inside the loop (an LVGL event
                            handler, the key handler, a posted callback or
                            an application event handler).
  @retval other             The loop stopped on a firmware error (also
                            reported by UefiGuiGetLastError()).
**/
EFI_STATUS
EFIAPI
UefiGuiRun (
  VOID
  );

/**
  Process pending input and due LVGL timers once, sleeping at most the time
  until the next LVGL timer is due.

  @retval EFI_SUCCESS        Processed.
  @retval EFI_ABORTED        An exit was requested.
  @retval EFI_NOT_STARTED    Not initialized.
  @retval EFI_ACCESS_DENIED  Called from inside the loop (re-entry).
  @retval other              SetTimer() or WaitForEvent() failed.
**/
EFI_STATUS
EFIAPI
UefiGuiProcessEvents (
  VOID
  );

/**
  Describe the active display.
**/
EFI_STATUS
EFIAPI
UefiGuiGetDisplayInfo (
  OUT UEFI_GUI_DISPLAY_INFO  *Info
  );

/**
  Return UEFI_GUI_INPUT_DEVICE_* bits for the input devices in use.
**/
UINT32
EFIAPI
UefiGuiGetInputDevices (
  VOID
  );

/**
  Ask UefiGuiRun() to return. UefiGuiRun() returns EFI_SUCCESS after the
  current iteration; UefiGuiProcessEvents() returns EFI_ABORTED from then on
  until UefiGuiRun() consumes the request.

  Callable from LVGL event handlers, the key handler, posted callbacks, and
  UEFI event notification functions at up to TPL_NOTIFY: it only sets a flag
  and signals an event, which wakes a sleeping loop at once.

  @retval EFI_SUCCESS      Exit requested.
  @retval EFI_NOT_STARTED  Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiRequestExit (
  VOID
  );

/**
  Return the most recent non-fatal or fatal error recorded by any backend.

  @param[out]  Status     Last error, EFI_SUCCESS if none.
**/
EFI_STATUS
EFIAPI
UefiGuiGetLastError (
  OUT EFI_STATUS  *Status
  );

/**
  Stop timers, close events, release backends and deinitialize LVGL. Safe to
  call more than once and after a failed UefiGuiInitialize(). Must not be
  called from inside the loop (an LVGL event handler, the key handler, a
  posted callback or an application event handler): there it does nothing
  and records EFI_ACCESS_DENIED; call UefiGuiRequestExit() instead and shut
  down after UefiGuiRun() returns.

  Releases, in order: the LVGL input devices, group and cursor; the input
  queue; all LVGL objects, the display and its draw buffers (lv_deinit());
  the loop's timer and events (queued callbacks are discarded, registered
  application events are forgotten but not closed); the GOP Blt buffer;
  and it restores the GOP mode that was active before UefiGuiInitialize()
  when a different mode was selected.
**/
VOID
EFIAPI
UefiGuiShutdown (
  VOID
  );

/**
  Read the rendering statistics.

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
  @retval EFI_NOT_STARTED        Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiGetRenderStats (
  OUT UEFI_GUI_RENDER_STATS  *Stats
  );

/**
  Debug and test aid: make the next Count flushes fail with
  EFI_DEVICE_ERROR before they reach the display, to exercise the error path
  (LVGL is still told the flush finished). 0 cancels.
**/
VOID
EFIAPI
UefiGuiDebugInjectFlushFailures (
  IN UINT32  Count
  );

/**
  Key handler: called in the GUI context for every key press before LVGL
  sees it. Return TRUE to consume the key (LVGL then never sees it).

  @param[in]  Key      The key (Pressed is TRUE).
  @param[in]  Context  Value passed to UefiGuiSetKeyHandler().
**/
typedef
BOOLEAN
(EFIAPI *UEFI_GUI_KEY_HANDLER)(
  IN CONST UEFI_GUI_KEY_EVENT  *Key,
  IN VOID                      *Context
  );

/**
  Install (or with NULL remove) the application key handler.
**/
VOID
EFIAPI
UefiGuiSetKeyHandler (
  IN UEFI_GUI_KEY_HANDLER  Handler  OPTIONAL,
  IN VOID                  *Context OPTIONAL
  );

/**
  Map a key to the value the LVGL keypad input device reports.

  @return  An LV_KEY_* value, the UTF-8 code of a printable character
           (packed as LVGL expects), or 0 when the key has no LVGL meaning
           (Ctrl/Alt combinations, Page Up/Down, function keys).
**/
UINT32
EFIAPI
UefiGuiKeyToLvgl (
  IN CONST UEFI_GUI_KEY_EVENT  *Key
  );

/**
  LVGL event code sent for a right click, registered by UefiGuiInitialize()
  with lv_event_register_id(). 0 before initialization.
**/
UINT32
EFIAPI
UefiGuiGetRightClickEventCode (
  VOID
  );

/**
  Read the input statistics.

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
  @retval EFI_NOT_STARTED        Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiGetInputStats (
  OUT UEFI_GUI_INPUT_STATS  *Stats
  );

/**
  Function run in the GUI context (TPL_APPLICATION) by the event loop.

  @param[in]  Context   Value given when the function was posted.
**/
typedef
VOID
(EFIAPI *UEFI_GUI_CALLBACK)(
  IN VOID  *Context
  );

/**
  Run Callback once, in the GUI context, at the start of the next loop
  iteration, waking the loop if it sleeps. Callbacks run in the order they
  were posted. Callbacks still queued at UefiGuiShutdown() are discarded
  (counted in CallbacksDropped).

  This is how code running at a raised TPL (UEFI event notification
  functions) hands work to the GUI: it may be called at up to TPL_NOTIFY.
  It allocates nothing.

  @retval EFI_SUCCESS            Queued.
  @retval EFI_INVALID_PARAMETER  Callback is NULL.
  @retval EFI_NOT_STARTED        Not initialized.
  @retval EFI_OUT_OF_RESOURCES   UEFI_GUI_MAX_POSTED_CALLBACKS are already
                                 queued (counted in CallbacksRejected).
**/
EFI_STATUS
EFIAPI
UefiGuiPostCallback (
  IN UEFI_GUI_CALLBACK  Callback,
  IN VOID               *Context  OPTIONAL
  );

///
/// Capacity of the posted callback queue.
///
#define UEFI_GUI_MAX_POSTED_CALLBACKS  32

/**
  Handler for an application event, run in the GUI context.

  @param[in]  Event     The event that was signaled.
  @param[in]  Context   Value given to UefiGuiRegisterEvent().
**/
typedef
VOID
(EFIAPI *UEFI_GUI_EVENT_HANDLER)(
  IN EFI_EVENT  Event,
  IN VOID       *Context
  );

///
/// Number of application events that can be registered at once.
///
#define UEFI_GUI_MAX_APP_EVENTS  8

/**
  Add an application owned event to the set the loop waits on. When it is
  signaled, the loop wakes and calls Handler in the GUI context, where LVGL
  may be used. The signal is consumed (as by CheckEvent()) before Handler
  runs.

  The event must be one WaitForEvent() accepts: created without
  EVT_NOTIFY_SIGNAL (for example type 0, EVT_TIMER, or a protocol's wait
  event). The application keeps ownership: unregister it before closing it.
  UefiGuiShutdown() unregisters every event but closes none.

  @retval EFI_SUCCESS            Registered.
  @retval EFI_INVALID_PARAMETER  Event or Handler is NULL, or Event is of
                                 type EVT_NOTIFY_SIGNAL.
  @retval EFI_ALREADY_STARTED    Event is already registered.
  @retval EFI_OUT_OF_RESOURCES   UEFI_GUI_MAX_APP_EVENTS are registered.
  @retval EFI_NOT_STARTED        Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiRegisterEvent (
  IN EFI_EVENT               Event,
  IN UEFI_GUI_EVENT_HANDLER  Handler,
  IN VOID                    *Context  OPTIONAL
  );

/**
  Remove an event added with UefiGuiRegisterEvent(). The event is not
  closed. Safe to call from the event's own handler.

  @retval EFI_SUCCESS      Removed.
  @retval EFI_NOT_FOUND    Event is not registered.
  @retval EFI_NOT_STARTED  Not initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiUnregisterEvent (
  IN EFI_EVENT  Event
  );

/**
  Read the event loop statistics. After UefiGuiShutdown() this returns the
  final statistics of the session that ended (OpenEvents is then the number
  of loop events that could not be closed, normally 0).

  @retval EFI_SUCCESS            Stats filled.
  @retval EFI_INVALID_PARAMETER  Stats is NULL.
  @retval EFI_NOT_STARTED        No session was ever initialized.
**/
EFI_STATUS
EFIAPI
UefiGuiGetLoopStats (
  OUT UEFI_GUI_LOOP_STATS  *Stats
  );

#endif // UEFI_GUI_LIB_H_
