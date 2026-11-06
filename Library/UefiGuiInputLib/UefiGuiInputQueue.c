/** @file
  Unified input event queue: a fixed-size ring buffer owned by the GUI
  context. No locking is needed because no notify function touches it.

  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "UefiGuiInputInternal.h"

/**
  Event kind bit of an event type.
**/
STATIC
UINT32
UefiGuiInputKindOf (
  IN UEFI_GUI_INPUT_TYPE  Type
  )
{
  return (Type == UEFI_GUI_INPUT_KEY) ? UEFI_GUI_INPUT_KIND_KEY : UEFI_GUI_INPUT_KIND_POINTER;
}

/**
  Ring index of the Offset-th oldest entry.
**/
STATIC
UINT32
UefiGuiInputSlot (
  IN UINT32  Offset
  )
{
  return (gUefiGuiInput.Head + Offset) % gUefiGuiInput.Capacity;
}

/**
  @return  Free queue slots.
**/
UINT32
UefiGuiInputFreeSlots (
  VOID
  )
{
  return gUefiGuiInput.Capacity - gUefiGuiInput.Count;
}

/**
  Append an event, stamping its time. Full queue: drop and count.
**/
BOOLEAN
UefiGuiInputQueueAppend (
  IN CONST UEFI_GUI_INPUT_EVENT  *Event
  )
{
  UEFI_GUI_INPUT_EVENT  *Slot;

  if ((gUefiGuiInput.Queue == NULL) || (gUefiGuiInput.Count >= gUefiGuiInput.Capacity)) {
    gUefiGuiInput.Stats.DroppedEvents++;
    return FALSE;
  }

  Slot = &gUefiGuiInput.Queue[UefiGuiInputSlot (gUefiGuiInput.Count)];
  CopyMem (Slot, Event, sizeof (*Slot));
  if (Slot->TimestampMs == 0) {
    Slot->TimestampMs = UefiGuiGetTimeMs ();
  }

  gUefiGuiInput.Count++;
  gUefiGuiInput.Stats.QueuedEvents++;
  if (gUefiGuiInput.Count > gUefiGuiInput.Stats.QueuePeak) {
    gUefiGuiInput.Stats.QueuePeak = gUefiGuiInput.Count;
  }

  return TRUE;
}

/**
  Queue a move to (X, Y). When the newest queued event is a move, it is
  updated in place: only the final position of a burst matters, and the
  merge keeps a fast mouse from filling the queue.
**/
BOOLEAN
UefiGuiInputQueueMove (
  IN INT32  X,
  IN INT32  Y
  )
{
  UEFI_GUI_INPUT_EVENT  *Tail;
  UEFI_GUI_INPUT_EVENT  Event;

  if (gUefiGuiInput.Count > 0) {
    Tail = &gUefiGuiInput.Queue[UefiGuiInputSlot (gUefiGuiInput.Count - 1)];
    if (Tail->Type == UEFI_GUI_INPUT_POINTER_MOVE) {
      Tail->u.Move.X = X;
      Tail->u.Move.Y = Y;
      gUefiGuiInput.Stats.CoalescedMoves++;
      return TRUE;
    }
  }

  ZeroMem (&Event, sizeof (Event));
  Event.Type       = UEFI_GUI_INPUT_POINTER_MOVE;
  Event.u.Move.X   = X;
  Event.u.Move.Y   = Y;
  return UefiGuiInputQueueAppend (&Event);
}

/**
  Add an event as if a device had reported it.
**/
BOOLEAN
EFIAPI
UefiGuiInputEnqueue (
  IN CONST UEFI_GUI_INPUT_EVENT  *Event
  )
{
  UEFI_GUI_INPUT_EVENT  Copy;

  if (!gUefiGuiInput.Initialized || (Event == NULL)) {
    return FALSE;
  }

  CopyMem (&Copy, Event, sizeof (Copy));
  switch (Copy.Type) {
    case UEFI_GUI_INPUT_KEY:
    case UEFI_GUI_INPUT_POINTER_WHEEL:
      return UefiGuiInputQueueAppend (&Copy);

    case UEFI_GUI_INPUT_POINTER_MOVE:
      gUefiGuiInput.CursorX = UefiGuiClamp (Copy.u.Move.X, gUefiGuiInput.Width - 1);
      gUefiGuiInput.CursorY = UefiGuiClamp (Copy.u.Move.Y, gUefiGuiInput.Height - 1);
      return UefiGuiInputQueueMove (gUefiGuiInput.CursorX, gUefiGuiInput.CursorY);

    case UEFI_GUI_INPUT_POINTER_BUTTON:
      if ((Copy.u.Button.Button != UEFI_GUI_POINTER_BUTTON_LEFT) &&
          (Copy.u.Button.Button != UEFI_GUI_POINTER_BUTTON_RIGHT))
      {
        return FALSE;
      }

      if (Copy.u.Button.Pressed) {
        gUefiGuiInput.Buttons |= Copy.u.Button.Button;
      } else {
        gUefiGuiInput.Buttons &= ~Copy.u.Button.Button;
      }

      return UefiGuiInputQueueAppend (&Copy);

    default:
      return FALSE;
  }
}

/**
  Remove the oldest event of the given kinds, keeping the others in order.
**/
BOOLEAN
EFIAPI
UefiGuiInputDequeueKind (
  IN  UINT32                Kinds,
  OUT UEFI_GUI_INPUT_EVENT  *Event
  )
{
  UINT32  Offset;
  UINT32  Index;

  if ((Event == NULL) || (gUefiGuiInput.Queue == NULL)) {
    return FALSE;
  }

  for (Offset = 0; Offset < gUefiGuiInput.Count; Offset++) {
    if ((UefiGuiInputKindOf (gUefiGuiInput.Queue[UefiGuiInputSlot (Offset)].Type) & Kinds) == 0) {
      continue;
    }

    CopyMem (Event, &gUefiGuiInput.Queue[UefiGuiInputSlot (Offset)], sizeof (*Event));
    //
    // Close the gap: shift the newer entries one slot toward the head.
    //
    for (Index = Offset; Index + 1 < gUefiGuiInput.Count; Index++) {
      CopyMem (
        &gUefiGuiInput.Queue[UefiGuiInputSlot (Index)],
        &gUefiGuiInput.Queue[UefiGuiInputSlot (Index + 1)],
        sizeof (UEFI_GUI_INPUT_EVENT)
        );
    }

    gUefiGuiInput.Count--;
    if (gUefiGuiInput.Count == 0) {
      gUefiGuiInput.Head = 0;
    }

    return TRUE;
  }

  return FALSE;
}

/**
  Remove the oldest event.
**/
BOOLEAN
EFIAPI
UefiGuiInputDequeue (
  OUT UEFI_GUI_INPUT_EVENT  *Event
  )
{
  return UefiGuiInputDequeueKind (UEFI_GUI_INPUT_KIND_ALL, Event);
}

/**
  Number of queued events of the given kinds.
**/
UINT32
EFIAPI
UefiGuiInputPendingKind (
  IN UINT32  Kinds
  )
{
  UINT32  Offset;
  UINT32  Pending;

  Pending = 0;
  for (Offset = 0; Offset < gUefiGuiInput.Count; Offset++) {
    if ((UefiGuiInputKindOf (gUefiGuiInput.Queue[UefiGuiInputSlot (Offset)].Type) & Kinds) != 0) {
      Pending++;
    }
  }

  return Pending;
}
