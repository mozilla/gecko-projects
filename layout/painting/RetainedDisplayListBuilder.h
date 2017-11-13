/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef RETAINEDDISPLAYLISTBUILDER_H_
#define RETAINEDDISPLAYLISTBUILDER_H_

#include "nsDisplayList.h"
#include "mozilla/Maybe.h"

struct RetainedDisplayListBuilder {
  RetainedDisplayListBuilder(nsIFrame* aReferenceFrame,
                             nsDisplayListBuilderMode aMode,
                             bool aBuildCaret)
    : mBuilder(aReferenceFrame, aMode, aBuildCaret, true)
    , mList(&mBuilder)
  {}
  ~RetainedDisplayListBuilder()
  {
    mList.DeleteAll(&mBuilder);
  }

  nsDisplayListBuilder* Builder() { return &mBuilder; }

  nsDisplayList* List() { return &mList; }

  bool AttemptPartialUpdate(nscolor aBackstop);

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(Cached, RetainedDisplayListBuilder)

private:
  void PreProcessDisplayList(nsDisplayList* aList, AnimatedGeometryRoot* aAGR);

  void MergeDisplayLists(nsDisplayList* aNewList,
                         nsDisplayList* aOldList,
                         nsDisplayList* aOutList,
                         mozilla::Maybe<const mozilla::ActiveScrolledRoot*>& aOutContainerASR);

  bool ComputeRebuildRegion(nsTArray<nsIFrame*>& aModifiedFrames,
                            nsRect* aOutDirty,
                            AnimatedGeometryRoot** aOutModifiedAGR,
                            nsTArray<nsIFrame*>* aOutFramesWithProps);

  void IncrementSubDocPresShellPaintCount(nsDisplayItem* aItem);

  nsDisplayListBuilder mBuilder;
  nsDisplayList mList;
  WeakFrame mPreviousCaret;

};

#endif // RETAINEDDISPLAYLISTBUILDER_H_
