/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DISPLAYITEMCLIPCHAIN_H_
#define DISPLAYITEMCLIPCHAIN_H_

#include "mozilla/Assertions.h"
#include "DisplayItemClip.h"
#include "nsString.h"

class nsIScrollableFrame;

namespace mozilla {

struct ActiveScrolledRoot;

/**
 * A DisplayItemClipChain is a linked list of DisplayItemClips where each clip
 * is associated with an active scrolled root that describes what the clip
 * moves with.
 * We use a chain instead of just one intersected clip due to async scrolling:
 * A clip that moves along with a display item can be fused to the item's
 * contents when drawing the layer contents, but all other clips in the chain
 * need to be kept separate so that they can be applied at composition time,
 * after any async scroll offsets have been applied.
 * The clip chain is created during display list construction by the builder's
 * DisplayListClipState.
 * The clip chain order is determined by the active scrolled root order.
 * For every DisplayItemClipChain object |clipChain|, the following holds:
 * !clipChain->mParent || ActiveScrolledRoot::IsAncestor(clipChain->mParent->mASR, clipChain->mASR).
 * The clip chain can skip over active scrolled roots. That just means that
 * there is no clip that moves with the skipped ASR in this chain.
 */
struct DisplayItemClipChain {

  /**
   * Get the display item clip in this chain that moves with aASR, or nullptr
   * if no such clip exists. aClipChain can be null.
   */
  static const DisplayItemClip* ClipForASR(const DisplayItemClipChain* aClipChain,
                                           const ActiveScrolledRoot* aASR);

  static bool Equal(const DisplayItemClipChain* aClip1, const DisplayItemClipChain* aClip2);

  static nsCString ToString(const DisplayItemClipChain* aClipChain);

  bool HasRoundedCorners() const;

  DisplayItemClip mClip;
  const ActiveScrolledRoot* mASR;
  const DisplayItemClipChain* mParent;
};

} // namespace mozilla

#endif /* DISPLAYITEMCLIPCHAIN_H_ */
