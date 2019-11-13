/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Finalization group GC implementation.
 */

#include "builtin/FinalizationGroupObject.h"
#include "gc/GCRuntime.h"
#include "gc/Zone.h"

#include "gc/PrivateIterators-inl.h"

using namespace js;
using namespace js::gc;

bool GCRuntime::registerWithFinalizationGroup(JSContext* cx,
                                              HandleObject target,
                                              HandleObject record) {
  MOZ_ASSERT(!IsCrossCompartmentWrapper(target));
  MOZ_ASSERT(
      UncheckedUnwrapWithoutExpose(record)->is<FinalizationRecordObject>());
  MOZ_ASSERT(target->compartment() == record->compartment());

  auto& map = target->zone()->finalizationRecordMap();
  auto ptr = map.lookupForAdd(target);
  if (!ptr) {
    if (!map.add(ptr, target, FinalizationRecordVector(target->zone()))) {
      ReportOutOfMemory(cx);
      return false;
    }
  }
  return ptr->value().append(record);
}

void GCRuntime::markFinalizationGroupData(JSTracer* trc) {
  // The finalization groups and holdings for all targets are marked as roots.
  for (GCZonesIter zone(this); !zone.done(); zone.next()) {
    auto& map = zone->finalizationRecordMap();
    for (Zone::FinalizationRecordMap::Enum e(map); !e.empty(); e.popFront()) {
      e.front().value().trace(trc);
    }
  }
}

void GCRuntime::sweepFinalizationGroups(Zone* zone) {
  // Queue holdings for cleanup for any entries whose target is dying and remove
  // them from the map. Sweep remaining unregister tokens.

  auto& map = zone->finalizationRecordMap();
  for (Zone::FinalizationRecordMap::Enum e(map); !e.empty(); e.popFront()) {
    auto& records = e.front().value();
    if (IsAboutToBeFinalized(&e.front().mutableKey())) {
      // Queue holdings for targets that are dying.
      for (JSObject* obj : records) {
        obj = UncheckedUnwrapWithoutExpose(obj);
        auto record = &obj->as<FinalizationRecordObject>();
        FinalizationGroupObject* group = record->group();
        if (group) {
          group->queueHoldingsToBeCleanedUp(record->holdings());
          queueFinalizationGroupForCleanup(group);
        }
      }
      e.removeFront();
    } else {
      // Update any pointers moved by the GC.
      records.sweep();
      // Remove records that have been unregistered.
      records.eraseIf([](JSObject* obj) {
        obj = UncheckedUnwrapWithoutExpose(obj);
        auto record = &obj->as<FinalizationRecordObject>();
        return !record->group();
      });
    }
  }
}

void GCRuntime::queueFinalizationGroupForCleanup(
    FinalizationGroupObject* group) {
  // Prod the embedding to call us back later to run the finalization callbacks.
  if (!group->isQueuedForCleanup()) {
    callHostCleanupFinalizationGroupCallback(group);
    group->setQueuedForCleanup(true);
  }
}

bool GCRuntime::cleanupQueuedFinalizationGroup(
    JSContext* cx, HandleFinalizationGroupObject group) {
  group->setQueuedForCleanup(false);
  bool ok = FinalizationGroupObject::cleanupQueuedHoldings(cx, group);
  return ok;
}
