/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WSRunObject.h"

#include "mozilla/Assertions.h"
#include "mozilla/Casting.h"
#include "mozilla/EditorDOMPoint.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/mozalloc.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/SelectionState.h"

#include "nsAString.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsISupportsImpl.h"
#include "nsRange.h"
#include "nsString.h"
#include "nsTextFragment.h"

namespace mozilla {

using namespace dom;

const char16_t kNBSP = 160;

template WSRunScanner::WSRunScanner(const HTMLEditor* aHTMLEditor,
                                    const EditorDOMPoint& aScanStartPoint,
                                    const EditorDOMPoint& aScanEndPoint);
template WSRunScanner::WSRunScanner(const HTMLEditor* aHTMLEditor,
                                    const EditorRawDOMPoint& aScanStartPoint,
                                    const EditorRawDOMPoint& aScanEndPoint);
template WSRunObject::WSRunObject(HTMLEditor& aHTMLEditor,
                                  const EditorDOMPoint& aScanStartPoint,
                                  const EditorDOMPoint& aScanEndPoint);
template WSRunObject::WSRunObject(HTMLEditor& aHTMLEditor,
                                  const EditorRawDOMPoint& aScanStartPoint,
                                  const EditorRawDOMPoint& aScanEndPoint);
template WSScanResult WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundaryFrom(
    const EditorDOMPoint& aPoint) const;
template WSScanResult WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundaryFrom(
    const EditorRawDOMPoint& aPoint) const;
template WSScanResult WSRunScanner::ScanNextVisibleNodeOrBlockBoundaryFrom(
    const EditorDOMPoint& aPoint) const;
template WSScanResult WSRunScanner::ScanNextVisibleNodeOrBlockBoundaryFrom(
    const EditorRawDOMPoint& aPoint) const;

template <typename PT, typename CT>
WSRunScanner::WSRunScanner(const HTMLEditor* aHTMLEditor,
                           const EditorDOMPointBase<PT, CT>& aScanStartPoint,
                           const EditorDOMPointBase<PT, CT>& aScanEndPoint)
    : mScanStartPoint(aScanStartPoint),
      mScanEndPoint(aScanEndPoint),
      mEditingHost(aHTMLEditor->GetActiveEditingHost()),
      mPRE(false),
      mStartOffset(0),
      mEndOffset(0),
      mFirstNBSPOffset(0),
      mLastNBSPOffset(0),
      mStartRun(nullptr),
      mEndRun(nullptr),
      mHTMLEditor(aHTMLEditor),
      mStartReason(WSType::none),
      mEndReason(WSType::none) {
  MOZ_ASSERT(
      *nsContentUtils::ComparePoints(aScanStartPoint.ToRawRangeBoundary(),
                                     aScanEndPoint.ToRawRangeBoundary()) <= 0);
  GetWSNodes();
  GetRuns();
}

WSRunScanner::~WSRunScanner() { ClearRuns(); }

template <typename PT, typename CT>
WSRunObject::WSRunObject(HTMLEditor& aHTMLEditor,
                         const EditorDOMPointBase<PT, CT>& aScanStartPoint,
                         const EditorDOMPointBase<PT, CT>& aScanEndPoint)
    : WSRunScanner(&aHTMLEditor, aScanStartPoint, aScanEndPoint),
      mHTMLEditor(aHTMLEditor) {}

// static
nsresult WSRunObject::Scrub(HTMLEditor& aHTMLEditor,
                            const EditorDOMPoint& aPoint) {
  MOZ_ASSERT(aPoint.IsSet());

  WSRunObject wsRunObject(aHTMLEditor, aPoint);
  nsresult rv = wsRunObject.Scrub();
  if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  return rv;
}

// static
nsresult WSRunObject::PrepareToJoinBlocks(HTMLEditor& aHTMLEditor,
                                          Element& aLeftBlockElement,
                                          Element& aRightBlockElement) {
  WSRunObject leftWSObj(aHTMLEditor,
                        EditorRawDOMPoint::AtEndOf(aLeftBlockElement));
  WSRunObject rightWSObj(aHTMLEditor,
                         EditorRawDOMPoint(&aRightBlockElement, 0));

  nsresult rv = leftWSObj.PrepareToDeleteRangePriv(&rightWSObj);
  if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  return rv;
}

nsresult WSRunObject::PrepareToDeleteRange(HTMLEditor& aHTMLEditor,
                                           EditorDOMPoint* aStartPoint,
                                           EditorDOMPoint* aEndPoint) {
  MOZ_ASSERT(aStartPoint);
  MOZ_ASSERT(aEndPoint);

  if (NS_WARN_IF(!aStartPoint->IsSet()) || NS_WARN_IF(!aEndPoint->IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoTrackDOMPoint trackerStart(aHTMLEditor.RangeUpdaterRef(), aStartPoint);
  AutoTrackDOMPoint trackerEnd(aHTMLEditor.RangeUpdaterRef(), aEndPoint);

  WSRunObject leftWSObj(aHTMLEditor, *aStartPoint);
  WSRunObject rightWSObj(aHTMLEditor, *aEndPoint);

  return leftWSObj.PrepareToDeleteRangePriv(&rightWSObj);
}

nsresult WSRunObject::PrepareToDeleteNode(HTMLEditor& aHTMLEditor,
                                          nsIContent* aContent) {
  if (NS_WARN_IF(!aContent)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsINode> parent = aContent->GetParentNode();
  NS_ENSURE_STATE(parent);
  int32_t offset = parent->ComputeIndexOf(aContent);

  WSRunObject leftWSObj(aHTMLEditor, parent, offset);
  WSRunObject rightWSObj(aHTMLEditor, parent, offset + 1);

  return leftWSObj.PrepareToDeleteRangePriv(&rightWSObj);
}

nsresult WSRunObject::PrepareToSplitAcrossBlocks(HTMLEditor& aHTMLEditor,
                                                 nsCOMPtr<nsINode>* aSplitNode,
                                                 int32_t* aSplitOffset) {
  if (NS_WARN_IF(!aSplitNode) || NS_WARN_IF(!*aSplitNode) ||
      NS_WARN_IF(!aSplitOffset)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoTrackDOMPoint tracker(aHTMLEditor.RangeUpdaterRef(), aSplitNode,
                            aSplitOffset);

  WSRunObject wsObj(aHTMLEditor, MOZ_KnownLive(*aSplitNode), *aSplitOffset);

  return wsObj.PrepareToSplitAcrossBlocksPriv();
}

already_AddRefed<Element> WSRunObject::InsertBreak(
    Selection& aSelection, const EditorDOMPoint& aPointToInsert,
    nsIEditor::EDirection aSelect) {
  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return nullptr;
  }

  // MOOSE: for now, we always assume non-PRE formatting.  Fix this later.
  // meanwhile, the pre case is handled in HandleInsertText() in
  // HTMLEditSubActionHandler.cpp

  WSFragment* beforeRun = FindNearestRun(aPointToInsert, false);
  WSFragment* afterRun = FindNearestRun(aPointToInsert, true);

  EditorDOMPoint pointToInsert(aPointToInsert);
  {
    // Some scoping for AutoTrackDOMPoint.  This will track our insertion
    // point while we tweak any surrounding whitespace
    AutoTrackDOMPoint tracker(mHTMLEditor.RangeUpdaterRef(), &pointToInsert);

    // Handle any changes needed to ws run after inserted br
    if (!afterRun || (afterRun->mType & WSType::trailingWS)) {
      // Don't need to do anything.  Just insert break.  ws won't change.
    } else if (afterRun->mType & WSType::leadingWS) {
      // Delete the leading ws that is after insertion point.  We don't
      // have to (it would still not be significant after br), but it's
      // just more aesthetically pleasing to.
      nsresult rv = DeleteRange(pointToInsert, afterRun->EndPoint());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }
    } else if (afterRun->mType == WSType::normalWS) {
      // Need to determine if break at front of non-nbsp run.  If so, convert
      // run to nbsp.
      EditorDOMPointInText atNextCharOfInsertionPoint =
          GetNextCharPoint(pointToInsert);
      if (atNextCharOfInsertionPoint.IsSet() &&
          !atNextCharOfInsertionPoint.IsEndOfContainer() &&
          atNextCharOfInsertionPoint.IsCharASCIISpace()) {
        EditorDOMPointInText atPreviousCharOfNextCharOfInsertionPoint =
            GetPreviousCharPointFromPointInText(atNextCharOfInsertionPoint);
        if (!atPreviousCharOfNextCharOfInsertionPoint.IsSet() ||
            atPreviousCharOfNextCharOfInsertionPoint.IsEndOfContainer() ||
            !atPreviousCharOfNextCharOfInsertionPoint.IsCharASCIISpace()) {
          // We are at start of non-nbsps.  Convert to a single nbsp.
          nsresult rv = InsertNBSPAndRemoveFollowingASCIIWhitespaces(
              atNextCharOfInsertionPoint);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return nullptr;
          }
        }
      }
    }

    // Handle any changes needed to ws run before inserted br
    if (!beforeRun || (beforeRun->mType & WSType::leadingWS)) {
      // Don't need to do anything.  Just insert break.  ws won't change.
    } else if (beforeRun->mType & WSType::trailingWS) {
      // Need to delete the trailing ws that is before insertion point, because
      // it would become significant after break inserted.
      nsresult rv = DeleteRange(beforeRun->StartPoint(), pointToInsert);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }
    } else if (beforeRun->mType == WSType::normalWS) {
      // Try to change an nbsp to a space, just to prevent nbsp proliferation
      nsresult rv = ReplacePreviousNBSPIfUnnecessary(beforeRun, pointToInsert);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }
    }
  }

  RefPtr<Element> newBrElement =
      MOZ_KnownLive(mHTMLEditor)
          .InsertBRElementWithTransaction(pointToInsert, aSelect);
  if (NS_WARN_IF(!newBrElement)) {
    return nullptr;
  }
  return newBrElement.forget();
}

nsresult WSRunObject::InsertText(Document& aDocument,
                                 const nsAString& aStringToInsert,
                                 EditorRawDOMPoint* aPointAfterInsertedString)
    MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION {
  // MOOSE: for now, we always assume non-PRE formatting.  Fix this later.
  // meanwhile, the pre case is handled in HandleInsertText() in
  // HTMLEditSubActionHandler.cpp

  // MOOSE: for now, just getting the ws logic straight.  This implementation
  // is very slow.  Will need to replace edit rules impl with a more efficient
  // text sink here that does the minimal amount of searching/replacing/copying

  if (aStringToInsert.IsEmpty()) {
    if (aPointAfterInsertedString) {
      *aPointAfterInsertedString = mScanStartPoint;
    }
    return NS_OK;
  }

  WSFragment* beforeRun = FindNearestRun(mScanStartPoint, false);
  // If mScanStartPoint isn't equal to mScanEndPoint, it will replace text (i.e.
  // committing composition). And afterRun will be end point of replaced range.
  // So we want to know this white space type (trailing whitespace etc) of
  // this end point, not inserted (start) point, so we re-scan white space type.
  WSRunObject afterRunObject(MOZ_KnownLive(mHTMLEditor), mScanEndPoint);
  WSFragment* afterRun = afterRunObject.FindNearestRun(mScanEndPoint, true);

  EditorDOMPoint pointToInsert(mScanStartPoint);
  nsAutoString theString(aStringToInsert);
  {
    // Some scoping for AutoTrackDOMPoint.  This will track our insertion
    // point while we tweak any surrounding whitespace
    AutoTrackDOMPoint tracker(mHTMLEditor.RangeUpdaterRef(), &pointToInsert);

    // Handle any changes needed to ws run after inserted text
    if (!afterRun || afterRun->mType & WSType::trailingWS) {
      // Don't need to do anything.  Just insert text.  ws won't change.
    } else if (afterRun->mType & WSType::leadingWS) {
      // Delete the leading ws that is after insertion point, because it
      // would become significant after text inserted.
      nsresult rv = DeleteRange(pointToInsert, afterRun->EndPoint());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    } else if (afterRun->mType == WSType::normalWS) {
      // Try to change an nbsp to a space, if possible, just to prevent nbsp
      // proliferation
      nsresult rv = CheckLeadingNBSP(
          afterRun, MOZ_KnownLive(pointToInsert.GetContainer()),
          pointToInsert.Offset());
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Handle any changes needed to ws run before inserted text
    if (!beforeRun || beforeRun->mType & WSType::leadingWS) {
      // Don't need to do anything.  Just insert text.  ws won't change.
    } else if (beforeRun->mType & WSType::trailingWS) {
      // Need to delete the trailing ws that is before insertion point, because
      // it would become significant after text inserted.
      nsresult rv = DeleteRange(beforeRun->StartPoint(), pointToInsert);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    } else if (beforeRun->mType == WSType::normalWS) {
      // Try to change an nbsp to a space, if possible, just to prevent nbsp
      // proliferation
      nsresult rv = ReplacePreviousNBSPIfUnnecessary(beforeRun, pointToInsert);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }

    // After this block, pointToInsert is modified by AutoTrackDOMPoint.
  }

  // Next up, tweak head and tail of string as needed.  First the head: there
  // are a variety of circumstances that would require us to convert a leading
  // ws char into an nbsp:

  if (nsCRT::IsAsciiSpace(theString[0])) {
    // We have a leading space
    if (beforeRun) {
      if (beforeRun->mType & WSType::leadingWS) {
        theString.SetCharAt(kNBSP, 0);
      } else if (beforeRun->mType & WSType::normalWS) {
        EditorDOMPointInText atPreviousChar =
            GetPreviousCharPoint(pointToInsert);
        if (atPreviousChar.IsSet() && !atPreviousChar.IsEndOfContainer() &&
            atPreviousChar.IsCharASCIISpace()) {
          theString.SetCharAt(kNBSP, 0);
        }
      }
    } else if (StartsFromHardLineBreak()) {
      theString.SetCharAt(kNBSP, 0);
    }
  }

  // Then the tail
  uint32_t lastCharIndex = theString.Length() - 1;

  if (nsCRT::IsAsciiSpace(theString[lastCharIndex])) {
    // We have a leading space
    if (afterRun) {
      if (afterRun->mType & WSType::trailingWS) {
        theString.SetCharAt(kNBSP, lastCharIndex);
      } else if (afterRun->mType & WSType::normalWS) {
        EditorDOMPointInText atNextChar = GetNextCharPoint(pointToInsert);
        if (atNextChar.IsSet() && !atNextChar.IsEndOfContainer() &&
            atNextChar.IsCharASCIISpace()) {
          theString.SetCharAt(kNBSP, lastCharIndex);
        }
      }
    } else if (afterRunObject.EndsByBlockBoundary()) {
      // When afterRun is null, it means that mScanEndPoint is last point in
      // editing host or editing block.
      // If this text insertion replaces composition, this.mEndReason is
      // start position of compositon. So we have to use afterRunObject's
      // reason instead.
      theString.SetCharAt(kNBSP, lastCharIndex);
    }
  }

  // Next, scan string for adjacent ws and convert to nbsp/space combos
  // MOOSE: don't need to convert tabs here since that is done by
  // WillInsertText() before we are called.  Eventually, all that logic will be
  // pushed down into here and made more efficient.
  bool prevWS = false;
  for (uint32_t i = 0; i <= lastCharIndex; i++) {
    if (nsCRT::IsAsciiSpace(theString[i])) {
      if (prevWS) {
        // i - 1 can't be negative because prevWS starts out false
        theString.SetCharAt(kNBSP, i - 1);
      } else {
        prevWS = true;
      }
    } else {
      prevWS = false;
    }
  }

  // XXX If the point is not editable, InsertTextWithTransaction() returns
  //     error, but we keep handling it.  But I think that it wastes the
  //     runtime cost.  So, perhaps, we should return error code which couldn't
  //     modify it and make each caller of this method decide whether it should
  //     keep or stop handling the edit action.
  nsresult rv =
      MOZ_KnownLive(mHTMLEditor)
          .InsertTextWithTransaction(aDocument, theString, pointToInsert,
                                     aPointAfterInsertedString);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // XXX Temporarily, set new insertion point to the original point.
    if (aPointAfterInsertedString) {
      *aPointAfterInsertedString = pointToInsert;
    }
    return NS_OK;
  }
  return NS_OK;
}

nsresult WSRunObject::DeleteWSBackward() {
  EditorDOMPointInText atPreviousCharOfStart =
      GetPreviousCharPoint(mScanStartPoint);
  if (!atPreviousCharOfStart.IsSet() ||
      atPreviousCharOfStart.IsEndOfContainer()) {
    return NS_OK;
  }

  // Easy case, preformatted ws.
  if (mPRE) {
    if (atPreviousCharOfStart.IsCharASCIISpace() ||
        atPreviousCharOfStart.IsCharNBSP()) {
      nsresult rv =
          DeleteRange(atPreviousCharOfStart, atPreviousCharOfStart.NextPoint());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
      return rv;
    }
    return NS_OK;
  }

  // Caller's job to ensure that previous char is really ws.  If it is normal
  // ws, we need to delete the whole run.
  if (atPreviousCharOfStart.IsCharASCIISpace()) {
    EditorDOMPointInText start, end;
    Tie(start, end) =
        GetASCIIWhitespacesBounds(eBoth, atPreviousCharOfStart.NextPoint());

    // adjust surrounding ws
    EditorDOMPoint startToDelete(start), endToDelete(end);
    nsresult rv = WSRunObject::PrepareToDeleteRange(
        MOZ_KnownLive(mHTMLEditor), &startToDelete, &endToDelete);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // finally, delete that ws
    rv = DeleteRange(startToDelete, endToDelete);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
    return rv;
  }

  if (atPreviousCharOfStart.IsCharNBSP()) {
    EditorDOMPoint startToDelete(atPreviousCharOfStart);
    EditorDOMPoint endToDelete(startToDelete.NextPoint());
    nsresult rv = WSRunObject::PrepareToDeleteRange(
        MOZ_KnownLive(mHTMLEditor), &startToDelete, &endToDelete);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // finally, delete that ws
    rv = DeleteRange(startToDelete, endToDelete);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
    return rv;
  }

  return NS_OK;
}

nsresult WSRunObject::DeleteWSForward() {
  EditorDOMPointInText atNextCharOfStart = GetNextCharPoint(mScanStartPoint);
  if (!atNextCharOfStart.IsSet() || atNextCharOfStart.IsEndOfContainer()) {
    return NS_OK;
  }

  // Easy case, preformatted ws.
  if (mPRE) {
    if (atNextCharOfStart.IsCharASCIISpace() ||
        atNextCharOfStart.IsCharNBSP()) {
      nsresult rv =
          DeleteRange(atNextCharOfStart, atNextCharOfStart.NextPoint());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
      return rv;
    }
    return NS_OK;
  }

  // Caller's job to ensure that next char is really ws.  If it is normal ws,
  // we need to delete the whole run.
  if (atNextCharOfStart.IsCharASCIISpace()) {
    EditorDOMPointInText start, end;
    Tie(start, end) =
        GetASCIIWhitespacesBounds(eBoth, atNextCharOfStart.NextPoint());
    // Adjust surrounding ws
    EditorDOMPoint startToDelete(start), endToDelete(end);
    nsresult rv = WSRunObject::PrepareToDeleteRange(
        MOZ_KnownLive(mHTMLEditor), &startToDelete, &endToDelete);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // Finally, delete that ws
    rv = DeleteRange(startToDelete, endToDelete);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
    return rv;
  }

  if (atNextCharOfStart.IsCharNBSP()) {
    EditorDOMPoint startToDelete(atNextCharOfStart);
    EditorDOMPoint endToDelete(startToDelete.NextPoint());
    nsresult rv = WSRunObject::PrepareToDeleteRange(
        MOZ_KnownLive(mHTMLEditor), &startToDelete, &endToDelete);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // Finally, delete that ws
    rv = DeleteRange(startToDelete, endToDelete);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
    return rv;
  }

  return NS_OK;
}

template <typename PT, typename CT>
WSScanResult WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundaryFrom(
    const EditorDOMPointBase<PT, CT>& aPoint) const {
  // Find first visible thing before the point.  Position
  // outVisNode/outVisOffset just _after_ that thing.  If we don't find
  // anything return start of ws.
  MOZ_ASSERT(aPoint.IsSet());

  WSFragment* run = FindNearestRun(aPoint, false);

  // Is there a visible run there or earlier?
  for (; run; run = run->mLeft) {
    if (run->mType == WSType::normalWS) {
      EditorDOMPointInText atPreviousChar = GetPreviousCharPoint(aPoint);
      // When it's a non-empty text node, return it.
      if (atPreviousChar.IsSet() && !atPreviousChar.IsContainerEmpty()) {
        MOZ_ASSERT(!atPreviousChar.IsEndOfContainer());
        return WSScanResult(
            atPreviousChar.NextPoint(),
            atPreviousChar.IsCharASCIISpace() || atPreviousChar.IsCharNBSP()
                ? WSType::normalWS
                : WSType::text);
      }
      // If no text node, keep looking.  We should eventually fall out of loop
    }
  }

  if (mStartReasonContent != mStartNode) {
    // In this case, mStartOffset is not meaningful.
    return WSScanResult(mStartReasonContent, mStartReason);
  }
  return WSScanResult(EditorDOMPoint(mStartReasonContent, mStartOffset),
                      mStartReason);
}

template <typename PT, typename CT>
WSScanResult WSRunScanner::ScanNextVisibleNodeOrBlockBoundaryFrom(
    const EditorDOMPointBase<PT, CT>& aPoint) const {
  // Find first visible thing after the point.  Position
  // outVisNode/outVisOffset just _before_ that thing.  If we don't find
  // anything return end of ws.
  MOZ_ASSERT(aPoint.IsSet());

  WSFragment* run = FindNearestRun(aPoint, true);

  // Is there a visible run there or later?
  for (; run; run = run->mRight) {
    if (run->mType == WSType::normalWS) {
      EditorDOMPointInText atNextChar = GetNextCharPoint(aPoint);
      // When it's a non-empty text node, return it.
      if (atNextChar.IsSet() && !atNextChar.IsContainerEmpty()) {
        return WSScanResult(
            atNextChar,
            !atNextChar.IsEndOfContainer() &&
                    (atNextChar.IsCharASCIISpace() || atNextChar.IsCharNBSP())
                ? WSType::normalWS
                : WSType::text);
      }
      // If no text node, keep looking.  We should eventually fall out of loop
    }
  }

  if (mEndReasonContent != mEndNode) {
    // In this case, mEndOffset is not meaningful.
    return WSScanResult(mEndReasonContent, mEndReason);
  }
  return WSScanResult(EditorDOMPoint(mEndReasonContent, mEndOffset),
                      mEndReason);
}

nsresult WSRunObject::AdjustWhitespace() {
  // this routine examines a run of ws and tries to get rid of some unneeded
  // nbsp's, replacing them with regualr ascii space if possible.  Keeping
  // things simple for now and just trying to fix up the trailing ws in the run.
  if (!mLastNBSPNode) {
    // nothing to do!
    return NS_OK;
  }
  WSFragment* curRun = mStartRun;
  while (curRun) {
    // look for normal ws run
    if (curRun->mType == WSType::normalWS) {
      nsresult rv = CheckTrailingNBSPOfRun(curRun);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
    curRun = curRun->mRight;
  }
  return NS_OK;
}

//--------------------------------------------------------------------------------------------
//   protected methods
//--------------------------------------------------------------------------------------------

nsIContent* WSRunScanner::GetEditableBlockParentOrTopmotEditableInlineContent(
    nsIContent* aContent) const {
  if (NS_WARN_IF(!aContent)) {
    return nullptr;
  }
  NS_ASSERTION(mHTMLEditor->IsEditable(aContent),
               "Given content is not editable");
  // XXX What should we do if scan range crosses block boundary?  Currently,
  //     it's not collapsed only when inserting composition string so that
  //     it's possible but shouldn't occur actually.
  nsIContent* editableBlockParentOrTopmotEditableInlineContent = nullptr;
  for (nsIContent* content = aContent;
       content && mHTMLEditor->IsEditable(content);
       content = content->GetParent()) {
    editableBlockParentOrTopmotEditableInlineContent = content;
    if (IsBlockNode(editableBlockParentOrTopmotEditableInlineContent)) {
      break;
    }
  }
  return editableBlockParentOrTopmotEditableInlineContent;
}

nsresult WSRunScanner::GetWSNodes() {
  // collect up an array of nodes that are contiguous with the insertion point
  // and which contain only whitespace.  Stop if you reach non-ws text or a new
  // block boundary.
  EditorDOMPoint start(mScanStartPoint), end(mScanStartPoint);
  nsIContent* scanStartContent = mScanStartPoint.GetContainerAsContent();
  if (NS_WARN_IF(!scanStartContent)) {
    // Meaning container of mScanStartPoint is a Document or DocumentFragment.
    // I.e., we're try to modify outside of root element.  We don't need to
    // support such odd case because web apps cannot append text nodes as
    // direct child of Document node.
    return NS_ERROR_FAILURE;
  }
  nsIContent* editableBlockParentOrTopmotEditableInlineContent =
      GetEditableBlockParentOrTopmotEditableInlineContent(scanStartContent);
  if (NS_WARN_IF(!editableBlockParentOrTopmotEditableInlineContent)) {
    // Meaning that the container of `mScanStartPoint` is not editable.
    editableBlockParentOrTopmotEditableInlineContent = scanStartContent;
  }

  // first look backwards to find preceding ws nodes
  if (Text* textNode = mScanStartPoint.GetContainerAsText()) {
    const nsTextFragment* textFrag = &textNode->TextFragment();
    mNodeArray.InsertElementAt(0, textNode);
    if (!mScanStartPoint.IsStartOfContainer()) {
      for (uint32_t i = mScanStartPoint.Offset(); i; i--) {
        // sanity bounds check the char position.  bug 136165
        if (i > textFrag->GetLength()) {
          MOZ_ASSERT_UNREACHABLE("looking beyond end of text fragment");
          continue;
        }
        char16_t theChar = textFrag->CharAt(i - 1);
        if (!nsCRT::IsAsciiSpace(theChar)) {
          if (theChar != kNBSP) {
            mStartNode = textNode;
            mStartOffset = i;
            mStartReason = WSType::text;
            mStartReasonContent = textNode;
            break;
          }
          // as we look backwards update our earliest found nbsp
          mFirstNBSPNode = textNode;
          mFirstNBSPOffset = i - 1;
          // also keep track of latest nbsp so far
          if (!mLastNBSPNode) {
            mLastNBSPNode = textNode;
            mLastNBSPOffset = i - 1;
          }
        }
        start.Set(textNode, i - 1);
      }
    }
  }

  while (!mStartNode) {
    // we haven't found the start of ws yet.  Keep looking
    nsCOMPtr<nsIContent> priorNode = GetPreviousWSNode(
        start, editableBlockParentOrTopmotEditableInlineContent);
    if (priorNode) {
      if (IsBlockNode(priorNode)) {
        mStartNode = start.GetContainer();
        mStartOffset = start.Offset();
        mStartReason = WSType::otherBlock;
        mStartReasonContent = priorNode;
      } else if (priorNode->IsText() && priorNode->IsEditable()) {
        RefPtr<Text> textNode = priorNode->GetAsText();
        mNodeArray.InsertElementAt(0, textNode);
        if (!textNode) {
          return NS_ERROR_NULL_POINTER;
        }
        const nsTextFragment* textFrag = &textNode->TextFragment();
        uint32_t len = textNode->TextLength();

        if (len < 1) {
          // Zero length text node. Set start point to it
          // so we can get past it!
          start.Set(priorNode, 0);
        } else {
          for (int32_t pos = len - 1; pos >= 0; pos--) {
            // sanity bounds check the char position.  bug 136165
            if (uint32_t(pos) >= textFrag->GetLength()) {
              MOZ_ASSERT_UNREACHABLE("looking beyond end of text fragment");
              continue;
            }
            char16_t theChar = textFrag->CharAt(pos);
            if (!nsCRT::IsAsciiSpace(theChar)) {
              if (theChar != kNBSP) {
                mStartNode = textNode;
                mStartOffset = pos + 1;
                mStartReason = WSType::text;
                mStartReasonContent = textNode;
                break;
              }
              // as we look backwards update our earliest found nbsp
              mFirstNBSPNode = textNode;
              mFirstNBSPOffset = pos;
              // also keep track of latest nbsp so far
              if (!mLastNBSPNode) {
                mLastNBSPNode = textNode;
                mLastNBSPOffset = pos;
              }
            }
            start.Set(textNode, pos);
          }
        }
      } else {
        // it's a break or a special node, like <img>, that is not a block and
        // not a break but still serves as a terminator to ws runs.
        mStartNode = start.GetContainer();
        mStartOffset = start.Offset();
        if (priorNode->IsHTMLElement(nsGkAtoms::br)) {
          mStartReason = WSType::br;
        } else {
          mStartReason = WSType::special;
        }
        mStartReasonContent = priorNode;
      }
    } else {
      // no prior node means we exhausted
      // editableBlockParentOrTopmotEditableInlineContent
      mStartNode = start.GetContainer();
      mStartOffset = start.Offset();
      mStartReason = WSType::thisBlock;
      // mStartReasonContent can be either a block element or any non-editable
      // content in this case.
      mStartReasonContent = editableBlockParentOrTopmotEditableInlineContent;
    }
  }

  // then look ahead to find following ws nodes
  if (Text* textNode = end.GetContainerAsText()) {
    // don't need to put it on list. it already is from code above
    const nsTextFragment* textFrag = &textNode->TextFragment();
    if (!end.IsEndOfContainer()) {
      for (uint32_t i = end.Offset(); i < textNode->TextLength(); i++) {
        // sanity bounds check the char position.  bug 136165
        if (i >= textFrag->GetLength()) {
          MOZ_ASSERT_UNREACHABLE("looking beyond end of text fragment");
          continue;
        }
        char16_t theChar = textFrag->CharAt(i);
        if (!nsCRT::IsAsciiSpace(theChar)) {
          if (theChar != kNBSP) {
            mEndNode = textNode;
            mEndOffset = i;
            mEndReason = WSType::text;
            mEndReasonContent = textNode;
            break;
          }
          // as we look forwards update our latest found nbsp
          mLastNBSPNode = textNode;
          mLastNBSPOffset = i;
          // also keep track of earliest nbsp so far
          if (!mFirstNBSPNode) {
            mFirstNBSPNode = textNode;
            mFirstNBSPOffset = i;
          }
        }
        end.Set(textNode, i + 1);
      }
    }
  }

  while (!mEndNode) {
    // we haven't found the end of ws yet.  Keep looking
    nsCOMPtr<nsIContent> nextNode =
        GetNextWSNode(end, editableBlockParentOrTopmotEditableInlineContent);
    if (nextNode) {
      if (IsBlockNode(nextNode)) {
        // we encountered a new block.  therefore no more ws.
        mEndNode = end.GetContainer();
        mEndOffset = end.Offset();
        mEndReason = WSType::otherBlock;
        mEndReasonContent = nextNode;
      } else if (nextNode->IsText() && nextNode->IsEditable()) {
        RefPtr<Text> textNode = nextNode->GetAsText();
        mNodeArray.AppendElement(textNode);
        if (!textNode) {
          return NS_ERROR_NULL_POINTER;
        }
        const nsTextFragment* textFrag = &textNode->TextFragment();
        uint32_t len = textNode->TextLength();

        if (len < 1) {
          // Zero length text node. Set end point to it
          // so we can get past it!
          end.Set(textNode, 0);
        } else {
          for (uint32_t pos = 0; pos < len; pos++) {
            // sanity bounds check the char position.  bug 136165
            if (pos >= textFrag->GetLength()) {
              MOZ_ASSERT_UNREACHABLE("looking beyond end of text fragment");
              continue;
            }
            char16_t theChar = textFrag->CharAt(pos);
            if (!nsCRT::IsAsciiSpace(theChar)) {
              if (theChar != kNBSP) {
                mEndNode = textNode;
                mEndOffset = pos;
                mEndReason = WSType::text;
                mEndReasonContent = textNode;
                break;
              }
              // as we look forwards update our latest found nbsp
              mLastNBSPNode = textNode;
              mLastNBSPOffset = pos;
              // also keep track of earliest nbsp so far
              if (!mFirstNBSPNode) {
                mFirstNBSPNode = textNode;
                mFirstNBSPOffset = pos;
              }
            }
            end.Set(textNode, pos + 1);
          }
        }
      } else {
        // we encountered a break or a special node, like <img>,
        // that is not a block and not a break but still
        // serves as a terminator to ws runs.
        mEndNode = end.GetContainer();
        mEndOffset = end.Offset();
        if (nextNode->IsHTMLElement(nsGkAtoms::br)) {
          mEndReason = WSType::br;
        } else {
          mEndReason = WSType::special;
        }
        mEndReasonContent = nextNode;
      }
    } else {
      // no next node means we exhausted
      // editableBlockParentOrTopmotEditableInlineContent
      mEndNode = end.GetContainer();
      mEndOffset = end.Offset();
      mEndReason = WSType::thisBlock;
      // mEndReasonContent can be either a block element or any non-editable
      // content in this case.
      mEndReasonContent = editableBlockParentOrTopmotEditableInlineContent;
    }
  }

  return NS_OK;
}

void WSRunScanner::GetRuns() {
  ClearRuns();

  // Handle preformatted case first since it's simple.  Note that if end of
  // the scan range isn't in preformatted element, we need to check only the
  // style at mScanStartPoint since the range would be replaced and the start
  // style will be applied to all new string.
  mPRE = EditorBase::IsPreformatted(mScanStartPoint.GetContainer());
  // if it's preformatedd, or if we are surrounded by text or special, it's all
  // one big normal ws run
  if (mPRE ||
      ((StartsFromNormalText() || StartsFromSpecialContent()) &&
       (EndsByNormalText() || EndsBySpecialContent() || EndsByBRElement()))) {
    MakeSingleWSRun(WSType::normalWS);
    return;
  }

  // if we are before or after a block (or after a break), and there are no
  // nbsp's, then it's all non-rendering ws.
  if (!mFirstNBSPNode && !mLastNBSPNode &&
      (StartsFromHardLineBreak() || EndsByBlockBoundary())) {
    WSType wstype;
    if (StartsFromHardLineBreak()) {
      wstype = WSType::leadingWS;
    }
    if (EndsByBlockBoundary()) {
      wstype |= WSType::trailingWS;
    }
    MakeSingleWSRun(wstype);
    return;
  }

  // otherwise a little trickier.  shucks.
  mStartRun = new WSFragment();
  mStartRun->mStartNode = mStartNode;
  mStartRun->mStartOffset = mStartOffset;

  if (StartsFromHardLineBreak()) {
    // set up mStartRun
    mStartRun->mType = WSType::leadingWS;
    mStartRun->mEndNode = mFirstNBSPNode;
    mStartRun->mEndOffset = mFirstNBSPOffset;
    mStartRun->mLeftType = mStartReason;
    mStartRun->mRightType = WSType::normalWS;

    // set up next run
    WSFragment* normalRun = new WSFragment();
    mStartRun->mRight = normalRun;
    normalRun->mType = WSType::normalWS;
    normalRun->mStartNode = mFirstNBSPNode;
    normalRun->mStartOffset = mFirstNBSPOffset;
    normalRun->mLeftType = WSType::leadingWS;
    normalRun->mLeft = mStartRun;
    if (!EndsByBlockBoundary()) {
      // then no trailing ws.  this normal run ends the overall ws run.
      normalRun->mRightType = mEndReason;
      normalRun->mEndNode = mEndNode;
      normalRun->mEndOffset = mEndOffset;
      mEndRun = normalRun;
    } else {
      // we might have trailing ws.
      // it so happens that *if* there is an nbsp at end,
      // {mEndNode,mEndOffset-1} will point to it, even though in general
      // start/end points not guaranteed to be in text nodes.
      if (mLastNBSPNode == mEndNode && mLastNBSPOffset == mEndOffset - 1) {
        // normal ws runs right up to adjacent block (nbsp next to block)
        normalRun->mRightType = mEndReason;
        normalRun->mEndNode = mEndNode;
        normalRun->mEndOffset = mEndOffset;
        mEndRun = normalRun;
      } else {
        normalRun->mEndNode = mLastNBSPNode;
        normalRun->mEndOffset = mLastNBSPOffset + 1;
        normalRun->mRightType = WSType::trailingWS;

        // set up next run
        WSFragment* lastRun = new WSFragment();
        lastRun->mType = WSType::trailingWS;
        lastRun->mStartNode = mLastNBSPNode;
        lastRun->mStartOffset = mLastNBSPOffset + 1;
        lastRun->mEndNode = mEndNode;
        lastRun->mEndOffset = mEndOffset;
        lastRun->mLeftType = WSType::normalWS;
        lastRun->mLeft = normalRun;
        lastRun->mRightType = mEndReason;
        mEndRun = lastRun;
        normalRun->mRight = lastRun;
      }
    }
  } else {
    MOZ_ASSERT(!StartsFromHardLineBreak());
    mStartRun->mType = WSType::normalWS;
    mStartRun->mEndNode = mLastNBSPNode;
    mStartRun->mEndOffset = mLastNBSPOffset + 1;
    mStartRun->mLeftType = mStartReason;

    // we might have trailing ws.
    // it so happens that *if* there is an nbsp at end, {mEndNode,mEndOffset-1}
    // will point to it, even though in general start/end points not
    // guaranteed to be in text nodes.
    if (mLastNBSPNode == mEndNode && mLastNBSPOffset == (mEndOffset - 1)) {
      mStartRun->mRightType = mEndReason;
      mStartRun->mEndNode = mEndNode;
      mStartRun->mEndOffset = mEndOffset;
      mEndRun = mStartRun;
    } else {
      // set up next run
      WSFragment* lastRun = new WSFragment();
      lastRun->mType = WSType::trailingWS;
      lastRun->mStartNode = mLastNBSPNode;
      lastRun->mStartOffset = mLastNBSPOffset + 1;
      lastRun->mLeftType = WSType::normalWS;
      lastRun->mLeft = mStartRun;
      lastRun->mRightType = mEndReason;
      mEndRun = lastRun;
      mStartRun->mRight = lastRun;
      mStartRun->mRightType = WSType::trailingWS;
    }
  }
}

void WSRunScanner::ClearRuns() {
  WSFragment *tmp, *run;
  run = mStartRun;
  while (run) {
    tmp = run->mRight;
    delete run;
    run = tmp;
  }
  mStartRun = 0;
  mEndRun = 0;
}

void WSRunScanner::MakeSingleWSRun(WSType aType) {
  mStartRun = new WSFragment();

  mStartRun->mStartNode = mStartNode;
  mStartRun->mStartOffset = mStartOffset;
  mStartRun->mType = aType;
  mStartRun->mEndNode = mEndNode;
  mStartRun->mEndOffset = mEndOffset;
  mStartRun->mLeftType = mStartReason;
  mStartRun->mRightType = mEndReason;

  mEndRun = mStartRun;
}

nsIContent* WSRunScanner::GetPreviousWSNodeInner(nsINode* aStartNode,
                                                 nsINode* aBlockParent) const {
  // Can't really recycle various getnext/prior routines because we have
  // special needs here.  Need to step into inline containers but not block
  // containers.
  MOZ_ASSERT(aStartNode && aBlockParent);

  if (NS_WARN_IF(aStartNode == mEditingHost)) {
    return nullptr;
  }

  nsCOMPtr<nsIContent> priorNode = aStartNode->GetPreviousSibling();
  OwningNonNull<nsINode> curNode = *aStartNode;
  while (!priorNode) {
    // We have exhausted nodes in parent of aStartNode.
    nsCOMPtr<nsINode> curParent = curNode->GetParentNode();
    if (NS_WARN_IF(!curParent)) {
      return nullptr;
    }
    if (curParent == aBlockParent) {
      // We have exhausted nodes in the block parent.  The convention here is
      // to return null.
      return nullptr;
    }
    if (NS_WARN_IF(curParent == mEditingHost)) {
      return nullptr;
    }
    // We have a parent: look for previous sibling
    priorNode = curParent->GetPreviousSibling();
    curNode = curParent;
  }
  // We have a prior node.  If it's a block, return it.
  if (IsBlockNode(priorNode)) {
    return priorNode;
  }
  if (mHTMLEditor->IsContainer(priorNode)) {
    // Else if it's a container, get deep rightmost child
    nsCOMPtr<nsIContent> child = mHTMLEditor->GetRightmostChild(priorNode);
    if (child) {
      return child;
    }
  }
  // Else return the node itself
  return priorNode;
}

nsIContent* WSRunScanner::GetPreviousWSNode(const EditorDOMPoint& aPoint,
                                            nsINode* aBlockParent) const {
  // Can't really recycle various getnext/prior routines because we
  // have special needs here.  Need to step into inline containers but
  // not block containers.
  MOZ_ASSERT(aPoint.IsSet() && aBlockParent);

  if (aPoint.IsInTextNode()) {
    return GetPreviousWSNodeInner(aPoint.GetContainer(), aBlockParent);
  }
  if (!mHTMLEditor->IsContainer(aPoint.GetContainer())) {
    return GetPreviousWSNodeInner(aPoint.GetContainer(), aBlockParent);
  }

  if (!aPoint.Offset()) {
    if (aPoint.GetContainer() == aBlockParent) {
      // We are at start of the block.
      return nullptr;
    }

    // We are at start of non-block container
    return GetPreviousWSNodeInner(aPoint.GetContainer(), aBlockParent);
  }

  if (NS_WARN_IF(!aPoint.GetContainerAsContent())) {
    return nullptr;
  }

  nsCOMPtr<nsIContent> priorNode = aPoint.GetPreviousSiblingOfChild();
  if (NS_WARN_IF(!priorNode)) {
    return nullptr;
  }

  // We have a prior node.  If it's a block, return it.
  if (IsBlockNode(priorNode)) {
    return priorNode;
  }
  if (mHTMLEditor->IsContainer(priorNode)) {
    // Else if it's a container, get deep rightmost child
    nsCOMPtr<nsIContent> child = mHTMLEditor->GetRightmostChild(priorNode);
    if (child) {
      return child;
    }
  }
  // Else return the node itself
  return priorNode;
}

nsIContent* WSRunScanner::GetNextWSNodeInner(nsINode* aStartNode,
                                             nsINode* aBlockParent) const {
  // Can't really recycle various getnext/prior routines because we have
  // special needs here.  Need to step into inline containers but not block
  // containers.
  MOZ_ASSERT(aStartNode && aBlockParent);

  if (NS_WARN_IF(aStartNode == mEditingHost)) {
    return nullptr;
  }

  nsCOMPtr<nsIContent> nextNode = aStartNode->GetNextSibling();
  nsCOMPtr<nsINode> curNode = aStartNode;
  while (!nextNode) {
    // We have exhausted nodes in parent of aStartNode.
    nsCOMPtr<nsINode> curParent = curNode->GetParentNode();
    if (NS_WARN_IF(!curParent)) {
      return nullptr;
    }
    if (curParent == aBlockParent) {
      // We have exhausted nodes in the block parent.  The convention here is
      // to return null.
      return nullptr;
    }
    if (NS_WARN_IF(curParent == mEditingHost)) {
      return nullptr;
    }
    // We have a parent: look for next sibling
    nextNode = curParent->GetNextSibling();
    curNode = curParent;
  }
  // We have a next node.  If it's a block, return it.
  if (IsBlockNode(nextNode)) {
    return nextNode;
  }
  if (mHTMLEditor->IsContainer(nextNode)) {
    // Else if it's a container, get deep leftmost child
    nsCOMPtr<nsIContent> child = mHTMLEditor->GetLeftmostChild(nextNode);
    if (child) {
      return child;
    }
  }
  // Else return the node itself
  return nextNode;
}

nsIContent* WSRunScanner::GetNextWSNode(const EditorDOMPoint& aPoint,
                                        nsINode* aBlockParent) const {
  // Can't really recycle various getnext/prior routines because we have
  // special needs here.  Need to step into inline containers but not block
  // containers.
  MOZ_ASSERT(aPoint.IsSet() && aBlockParent);

  if (aPoint.IsInTextNode()) {
    return GetNextWSNodeInner(aPoint.GetContainer(), aBlockParent);
  }
  if (!mHTMLEditor->IsContainer(aPoint.GetContainer())) {
    return GetNextWSNodeInner(aPoint.GetContainer(), aBlockParent);
  }

  if (NS_WARN_IF(!aPoint.GetContainerAsContent())) {
    return nullptr;
  }

  nsCOMPtr<nsIContent> nextNode = aPoint.GetChild();
  if (!nextNode) {
    if (aPoint.GetContainer() == aBlockParent) {
      // We are at end of the block.
      return nullptr;
    }

    // We are at end of non-block container
    return GetNextWSNodeInner(aPoint.GetContainer(), aBlockParent);
  }

  // We have a next node.  If it's a block, return it.
  if (IsBlockNode(nextNode)) {
    return nextNode;
  }
  if (mHTMLEditor->IsContainer(nextNode)) {
    // else if it's a container, get deep leftmost child
    nsCOMPtr<nsIContent> child = mHTMLEditor->GetLeftmostChild(nextNode);
    if (child) {
      return child;
    }
  }
  // Else return the node itself
  return nextNode;
}

nsresult WSRunObject::PrepareToDeleteRangePriv(WSRunObject* aEndObject) {
  // this routine adjust whitespace before *this* and after aEndObject
  // in preperation for the two areas to become adjacent after the
  // intervening content is deleted.  It's overly agressive right
  // now.  There might be a block boundary remaining between them after
  // the deletion, in which case these adjstments are unneeded (though
  // I don't think they can ever be harmful?)

  NS_ENSURE_TRUE(aEndObject, NS_ERROR_NULL_POINTER);

  // get the runs before and after selection
  WSFragment* beforeRun = FindNearestRun(mScanStartPoint, false);
  WSFragment* afterRun =
      aEndObject->FindNearestRun(aEndObject->mScanStartPoint, true);

  // trim after run of any leading ws
  if (afterRun && (afterRun->mType & WSType::leadingWS)) {
    nsresult rv = aEndObject->DeleteRange(aEndObject->mScanStartPoint,
                                          afterRun->EndPoint());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  // adjust normal ws in afterRun if needed
  if (afterRun && afterRun->mType == WSType::normalWS && !aEndObject->mPRE) {
    if ((beforeRun && (beforeRun->mType & WSType::leadingWS)) ||
        (!beforeRun && StartsFromHardLineBreak())) {
      // make sure leading char of following ws is an nbsp, so that it will show
      // up
      EditorDOMPointInText nextCharOfStartOfEnd =
          aEndObject->GetNextCharPoint(aEndObject->mScanStartPoint);
      if (nextCharOfStartOfEnd.IsSet() &&
          !nextCharOfStartOfEnd.IsEndOfContainer() &&
          nextCharOfStartOfEnd.IsCharASCIISpace()) {
        nsresult rv = aEndObject->InsertNBSPAndRemoveFollowingASCIIWhitespaces(
            nextCharOfStartOfEnd);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
  }
  // trim before run of any trailing ws
  if (beforeRun && (beforeRun->mType & WSType::trailingWS)) {
    nsresult rv = DeleteRange(beforeRun->StartPoint(), mScanStartPoint);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  } else if (beforeRun && beforeRun->mType == WSType::normalWS && !mPRE) {
    if ((afterRun && (afterRun->mType & WSType::trailingWS)) ||
        (afterRun && afterRun->mType == WSType::normalWS) ||
        (!afterRun && aEndObject->EndsByBlockBoundary())) {
      // make sure trailing char of starting ws is an nbsp, so that it will show
      // up
      EditorDOMPointInText atPreviousCharOfStart =
          GetPreviousCharPoint(mScanStartPoint);
      if (atPreviousCharOfStart.IsSet() &&
          !atPreviousCharOfStart.IsEndOfContainer() &&
          atPreviousCharOfStart.IsCharASCIISpace()) {
        EditorDOMPointInText start, end;
        Tie(start, end) = GetASCIIWhitespacesBounds(eBoth, mScanStartPoint);
        nsresult rv = InsertNBSPAndRemoveFollowingASCIIWhitespaces(start);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
  }
  return NS_OK;
}

nsresult WSRunObject::PrepareToSplitAcrossBlocksPriv() {
  // used to prepare ws to be split across two blocks.  The main issue
  // here is make sure normalWS doesn't end up becoming non-significant
  // leading or trailing ws after the split.

  // get the runs before and after selection
  WSFragment* beforeRun = FindNearestRun(mScanStartPoint, false);
  WSFragment* afterRun = FindNearestRun(mScanStartPoint, true);

  // adjust normal ws in afterRun if needed
  if (afterRun && afterRun->mType == WSType::normalWS) {
    // make sure leading char of following ws is an nbsp, so that it will show
    // up
    EditorDOMPointInText atNextCharOfStart = GetNextCharPoint(mScanStartPoint);
    if (atNextCharOfStart.IsSet() && !atNextCharOfStart.IsEndOfContainer() &&
        atNextCharOfStart.IsCharASCIISpace()) {
      nsresult rv =
          InsertNBSPAndRemoveFollowingASCIIWhitespaces(atNextCharOfStart);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  // adjust normal ws in beforeRun if needed
  if (beforeRun && beforeRun->mType == WSType::normalWS) {
    // make sure trailing char of starting ws is an nbsp, so that it will show
    // up
    EditorDOMPointInText atPreviousCharOfStart =
        GetPreviousCharPoint(mScanStartPoint);
    if (atPreviousCharOfStart.IsSet() &&
        !atPreviousCharOfStart.IsEndOfContainer() &&
        atPreviousCharOfStart.IsCharASCIISpace()) {
      EditorDOMPointInText start, end;
      Tie(start, end) = GetASCIIWhitespacesBounds(eBoth, mScanStartPoint);
      nsresult rv = InsertNBSPAndRemoveFollowingASCIIWhitespaces(start);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }
  return NS_OK;
}

nsresult WSRunObject::DeleteRange(const EditorDOMPoint& aStartPoint,
                                  const EditorDOMPoint& aEndPoint) {
  if (NS_WARN_IF(!aStartPoint.IsSet()) || NS_WARN_IF(!aEndPoint.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }
  MOZ_ASSERT(aStartPoint.IsSetAndValid());
  MOZ_ASSERT(aEndPoint.IsSetAndValid());

  // MOOSE: this routine needs to be modified to preserve the integrity of the
  // wsFragment info.

  if (aStartPoint == aEndPoint) {
    // Nothing to delete
    return NS_OK;
  }

  if (aStartPoint.GetContainer() == aEndPoint.GetContainer() &&
      aStartPoint.IsInTextNode()) {
    RefPtr<Text> textNode = aStartPoint.ContainerAsText();
    return MOZ_KnownLive(mHTMLEditor)
        .DeleteTextWithTransaction(*textNode, aStartPoint.Offset(),
                                   aEndPoint.Offset() - aStartPoint.Offset());
  }

  RefPtr<nsRange> range;
  int32_t count = mNodeArray.Length();
  int32_t idx = mNodeArray.IndexOf(aStartPoint.GetContainer());
  if (idx == -1) {
    // If our starting point wasn't one of our ws text nodes, then just go
    // through them from the beginning.
    idx = 0;
  }
  for (; idx < count; idx++) {
    RefPtr<Text> node = mNodeArray[idx];
    if (!node) {
      // We ran out of ws nodes; must have been deleting to end
      return NS_OK;
    }
    if (node == aStartPoint.GetContainer()) {
      if (!aStartPoint.IsEndOfContainer()) {
        nsresult rv = MOZ_KnownLive(mHTMLEditor)
                          .DeleteTextWithTransaction(
                              *node, aStartPoint.Offset(),
                              aStartPoint.GetContainer()->Length() -
                                  aStartPoint.Offset());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    } else if (node == aEndPoint.GetContainer()) {
      if (!aEndPoint.IsStartOfContainer()) {
        nsresult rv =
            MOZ_KnownLive(mHTMLEditor)
                .DeleteTextWithTransaction(*node, 0, aEndPoint.Offset());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
      break;
    } else {
      if (!range) {
        ErrorResult error;
        range = nsRange::Create(aStartPoint.ToRawRangeBoundary(),
                                aEndPoint.ToRawRangeBoundary(), error);
        if (NS_WARN_IF(!range)) {
          return error.StealNSResult();
        }
      }
      bool nodeBefore, nodeAfter;
      nsresult rv =
          RangeUtils::CompareNodeToRange(node, range, &nodeBefore, &nodeAfter);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      if (nodeAfter) {
        break;
      }
      if (!nodeBefore) {
        rv = MOZ_KnownLive(mHTMLEditor).DeleteNodeWithTransaction(*node);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
        mNodeArray.RemoveElement(node);
        --count;
        --idx;
      }
    }
  }
  return NS_OK;
}

template <typename PT, typename CT>
EditorDOMPointInText WSRunScanner::GetNextCharPoint(
    const EditorDOMPointBase<PT, CT>& aPoint) const {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  size_t index = aPoint.IsInTextNode()
                     ? mNodeArray.IndexOf(aPoint.GetContainer())
                     : decltype(mNodeArray)::NoIndex;
  if (index == decltype(mNodeArray)::NoIndex) {
    // Use range comparisons to get next text node which is in mNodeArray.
    return LookForNextCharPointWithinAllTextNodes(aPoint);
  }
  return GetNextCharPointFromPointInText(
      EditorDOMPointInText(mNodeArray[index], aPoint.Offset()));
}

template <typename PT, typename CT>
EditorDOMPointInText WSRunScanner::GetPreviousCharPoint(
    const EditorDOMPointBase<PT, CT>& aPoint) const {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  size_t index = aPoint.IsInTextNode()
                     ? mNodeArray.IndexOf(aPoint.GetContainer())
                     : decltype(mNodeArray)::NoIndex;
  if (index == decltype(mNodeArray)::NoIndex) {
    // Use range comparisons to get previous text node which is in mNodeArray.
    return LookForPreviousCharPointWithinAllTextNodes(aPoint);
  }
  return GetPreviousCharPointFromPointInText(
      EditorDOMPointInText(mNodeArray[index], aPoint.Offset()));
}

EditorDOMPointInText WSRunScanner::GetNextCharPointFromPointInText(
    const EditorDOMPointInText& aPoint) const {
  MOZ_ASSERT(aPoint.IsSet());

  size_t index = mNodeArray.IndexOf(aPoint.GetContainer());
  if (index == decltype(mNodeArray)::NoIndex) {
    // Can't find point, but it's not an error
    return EditorDOMPointInText();
  }

  if (aPoint.IsSetAndValid() && !aPoint.IsEndOfContainer()) {
    // XXX This may return empty text node.
    return aPoint;
  }

  if (index + 1 == mNodeArray.Length()) {
    return EditorDOMPointInText();
  }

  // XXX This may return empty text node.
  return EditorDOMPointInText(mNodeArray[index + 1], 0);
}

EditorDOMPointInText WSRunScanner::GetPreviousCharPointFromPointInText(
    const EditorDOMPointInText& aPoint) const {
  MOZ_ASSERT(aPoint.IsSet());

  size_t index = mNodeArray.IndexOf(aPoint.GetContainer());
  if (index == decltype(mNodeArray)::NoIndex) {
    // Can't find point, but it's not an error
    return EditorDOMPointInText();
  }

  if (!aPoint.IsStartOfContainer()) {
    return aPoint.PreviousPoint();
  }

  if (!index) {
    return EditorDOMPointInText();
  }

  // XXX This may return empty text node.
  return EditorDOMPointInText(mNodeArray[index - 1],
                              mNodeArray[index - 1]->TextLength()
                                  ? mNodeArray[index - 1]->TextLength() - 1
                                  : 0);
}

nsresult WSRunObject::InsertNBSPAndRemoveFollowingASCIIWhitespaces(
    const EditorDOMPointInText& aPoint) {
  // MOOSE: this routine needs to be modified to preserve the integrity of the
  // wsFragment info.
  if (NS_WARN_IF(!aPoint.IsSet())) {
    return NS_ERROR_NULL_POINTER;
  }

  // First, insert an NBSP.
  AutoTransactionsConserveSelection dontChangeMySelection(mHTMLEditor);
  nsresult rv = MOZ_KnownLive(mHTMLEditor)
                    .InsertTextIntoTextNodeWithTransaction(
                        nsDependentSubstring(&kNBSP, 1), aPoint, true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Now, the text node may have been modified by mutation observer.
  // So, the NBSP may have gone.
  if (!aPoint.IsSetAndValid() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNBSP()) {
    // This is just preparation of an edit action.  Let's return NS_OK.
    // XXX Perhaps, we should return another success code which indicates
    //     mutation observer touched the DOM tree.  However, that should
    //     be returned from each transaction's DoTransaction.
    return NS_OK;
  }

  // Next, find range of whitespaces it will be replaced.
  EditorDOMPointInText start, end;
  Tie(start, end) = GetASCIIWhitespacesBounds(eAfter, aPoint.NextPoint());

  // Finally, delete that replaced ws, if any
  if (start.IsSet()) {
    nsresult rv = DeleteRange(start, end);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  return NS_OK;
}

template <typename PT, typename CT>
Tuple<EditorDOMPointInText, EditorDOMPointInText>
WSRunObject::GetASCIIWhitespacesBounds(
    int16_t aDir, const EditorDOMPointBase<PT, CT>& aPoint) const {
  MOZ_ASSERT(aPoint.IsSet());

  EditorDOMPointInText start, end;

  if (aDir & eAfter) {
    EditorDOMPointInText atNextChar = GetNextCharPoint(aPoint);
    if (atNextChar.IsSet()) {
      // We found a text node, at least.
      start = end = atNextChar;
      // Scan ahead to end of ASCII whitespaces.
      // XXX Looks like that this is too expensive in most cases.  While we
      //     are scanning a text node, we should do it without
      //     GetNextCharPointInText().
      // XXX This loop ends at end of a text node.  Shouldn't we keep looking
      //     next text node?
      for (; atNextChar.IsSet() && !atNextChar.IsEndOfContainer() &&
             atNextChar.IsCharASCIISpace();
           atNextChar = GetNextCharPointFromPointInText(atNextChar)) {
        // End of the range should be after the whitespace.
        end = atNextChar = atNextChar.NextPoint();
      }
    }
  }

  if (aDir & eBefore) {
    EditorDOMPointInText atPreviousChar = GetPreviousCharPoint(aPoint);
    if (atPreviousChar.IsSet()) {
      // We found a text node, at least.
      start = atPreviousChar.NextPoint();
      if (!end.IsSet()) {
        end = start;
      }
      // Scan back to start of ASCII whitespaces.
      // XXX Looks like that this is too expensive in most cases.  While we
      //     are scanning a text node, we should do it without
      //     GetPreviousCharPointFromPointInText().
      // XXX This loop ends at end of a text node.  Shouldn't we keep looking
      //     the text node?
      for (; atPreviousChar.IsSet() && !atPreviousChar.IsEndOfContainer() &&
             atPreviousChar.IsCharASCIISpace();
           atPreviousChar =
               GetPreviousCharPointFromPointInText(atPreviousChar)) {
        start = atPreviousChar;
      }
    }
  }

  return MakeTuple(start, end);
}

template <typename PT, typename CT>
WSRunScanner::WSFragment* WSRunScanner::FindNearestRun(
    const EditorDOMPointBase<PT, CT>& aPoint, bool aForward) const {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  for (WSFragment* run = mStartRun; run; run = run->mRight) {
    int32_t comp = run->mStartNode ? *nsContentUtils::ComparePoints(
                                         aPoint.ToRawRangeBoundary(),
                                         run->StartPoint().ToRawRangeBoundary())
                                   : -1;
    if (comp <= 0) {
      // aPoint equals or before start of the run.  Return the run if we're
      // scanning forward, otherwise, nullptr.
      return aForward ? run : nullptr;
    }

    comp = run->mEndNode ? *nsContentUtils::ComparePoints(
                               aPoint.ToRawRangeBoundary(),
                               run->EndPoint().ToRawRangeBoundary())
                         : -1;
    if (comp < 0) {
      // If aPoint is in the run, return the run.
      return run;
    }

    if (!comp) {
      // If aPoint is at end of the run, return next run if we're scanning
      // forward, otherwise, return the run.
      return aForward ? run->mRight : run;
    }

    if (!run->mRight) {
      // If the run is the last run and aPoint is after end of the last run,
      // return nullptr if we're scanning forward, otherwise, return this
      // last run.
      return aForward ? nullptr : run;
    }
  }

  return nullptr;
}

char16_t WSRunScanner::GetCharAt(Text* aTextNode, int32_t aOffset) const {
  // return 0 if we can't get a char, for whatever reason
  NS_ENSURE_TRUE(aTextNode, 0);

  int32_t len = int32_t(aTextNode->TextDataLength());
  if (aOffset < 0 || aOffset >= len) {
    return 0;
  }
  return aTextNode->TextFragment().CharAt(aOffset);
}

template <typename PT, typename CT>
EditorDOMPointInText WSRunScanner::LookForNextCharPointWithinAllTextNodes(
    const EditorDOMPointBase<PT, CT>& aPoint) const {
  // Note: only to be called if aPoint.GetContainer() is not a ws node.

  MOZ_ASSERT(aPoint.IsSetAndValid());

  // Binary search on wsnodes
  uint32_t numNodes = mNodeArray.Length();

  if (!numNodes) {
    // Do nothing if there are no nodes to search
    return EditorDOMPointInText();
  }

  // Begin binary search.  We do this because we need to minimize calls to
  // ComparePoints(), which is expensive.
  uint32_t firstNum = 0, curNum = numNodes / 2, lastNum = numNodes;
  while (curNum != lastNum) {
    Text* curNode = mNodeArray[curNum];
    int16_t cmp = *nsContentUtils::ComparePoints(aPoint.ToRawRangeBoundary(),
                                                 RawRangeBoundary(curNode, 0u));
    if (cmp < 0) {
      lastNum = curNum;
    } else {
      firstNum = curNum + 1;
    }
    curNum = (lastNum - firstNum) / 2 + firstNum;
    MOZ_ASSERT(firstNum <= curNum && curNum <= lastNum, "Bad binary search");
  }

  // When the binary search is complete, we always know that the current node
  // is the same as the end node, which is always past our range.  Therefore,
  // we've found the node immediately after the point of interest.
  if (curNum == mNodeArray.Length()) {
    // hey asked for past our range (it's after the last node).
    // GetNextCharPoint() will do the work for us when we pass it the last
    // index of the last node.
    return GetNextCharPointFromPointInText(
        EditorDOMPointInText::AtEndOf(mNodeArray[curNum - 1]));
  }

  // The char after the point is the first character of our range.
  return GetNextCharPointFromPointInText(
      EditorDOMPointInText(mNodeArray[curNum], 0));
}

template <typename PT, typename CT>
EditorDOMPointInText WSRunScanner::LookForPreviousCharPointWithinAllTextNodes(
    const EditorDOMPointBase<PT, CT>& aPoint) const {
  // Note: only to be called if aNode is not a ws node.

  MOZ_ASSERT(aPoint.IsSetAndValid());

  // Binary search on wsnodes
  uint32_t numNodes = mNodeArray.Length();

  if (!numNodes) {
    // Do nothing if there are no nodes to search
    return EditorDOMPointInText();
  }

  uint32_t firstNum = 0, curNum = numNodes / 2, lastNum = numNodes;
  int16_t cmp = 0;

  // Begin binary search.  We do this because we need to minimize calls to
  // ComparePoints(), which is expensive.
  while (curNum != lastNum) {
    Text* curNode = mNodeArray[curNum];
    cmp = *nsContentUtils::ComparePoints(aPoint.ToRawRangeBoundary(),
                                         RawRangeBoundary(curNode, 0u));
    if (cmp < 0) {
      lastNum = curNum;
    } else {
      firstNum = curNum + 1;
    }
    curNum = (lastNum - firstNum) / 2 + firstNum;
    MOZ_ASSERT(firstNum <= curNum && curNum <= lastNum, "Bad binary search");
  }

  // When the binary search is complete, we always know that the current node
  // is the same as the end node, which is always past our range. Therefore,
  // we've found the node immediately after the point of interest.
  if (curNum == mNodeArray.Length()) {
    // Get the point before the end of the last node, we can pass the length of
    // the node into GetPreviousCharPoint(), and it will return the last
    // character.
    return GetPreviousCharPointFromPointInText(
        EditorDOMPointInText::AtEndOf(mNodeArray[curNum - 1]));
  }

  // We can just ask the current node for the point immediately before it,
  // it will handle moving to the previous node (if any) and returning the
  // appropriate character
  return GetPreviousCharPointFromPointInText(
      EditorDOMPointInText(mNodeArray[curNum], 0));
}

nsresult WSRunObject::CheckTrailingNBSPOfRun(WSFragment* aRun) {
  // Try to change an nbsp to a space, if possible, just to prevent nbsp
  // proliferation.  Examine what is before and after the trailing nbsp, if
  // any.
  NS_ENSURE_TRUE(aRun, NS_ERROR_NULL_POINTER);
  bool leftCheck = false;
  bool spaceNBSP = false;
  bool rightCheck = false;

  // confirm run is normalWS
  if (aRun->mType != WSType::normalWS) {
    return NS_ERROR_FAILURE;
  }

  // first check for trailing nbsp
  EditorDOMPointInText atPreviousCharOfEndOfRun =
      GetPreviousCharPoint(aRun->EndPoint());
  if (atPreviousCharOfEndOfRun.IsSet() &&
      !atPreviousCharOfEndOfRun.IsEndOfContainer() &&
      atPreviousCharOfEndOfRun.IsCharNBSP()) {
    // now check that what is to the left of it is compatible with replacing
    // nbsp with space
    EditorDOMPointInText atPreviousCharOfPreviousCharOfEndOfRun =
        GetPreviousCharPointFromPointInText(atPreviousCharOfEndOfRun);
    if (atPreviousCharOfPreviousCharOfEndOfRun.IsSet()) {
      if (atPreviousCharOfPreviousCharOfEndOfRun.IsEndOfContainer() ||
          !atPreviousCharOfPreviousCharOfEndOfRun.IsCharASCIISpace()) {
        leftCheck = true;
      } else {
        spaceNBSP = true;
      }
    } else if (aRun->mLeftType == WSType::text ||
               aRun->mLeftType == WSType::special) {
      leftCheck = true;
    }
    if (leftCheck || spaceNBSP) {
      // now check that what is to the right of it is compatible with replacing
      // nbsp with space
      if (aRun->mRightType == WSType::text ||
          aRun->mRightType == WSType::special ||
          aRun->mRightType == WSType::br) {
        rightCheck = true;
      }
      if ((aRun->mRightType & WSType::block) &&
          (IsBlockNode(GetEditableBlockParentOrTopmotEditableInlineContent(
               mScanStartPoint.GetContainerAsContent())) ||
           IsBlockNode(mScanStartPoint.GetContainerAsContent()))) {
        // We are at a block boundary.  Insert a <br>.  Why?  Well, first note
        // that the br will have no visible effect since it is up against a
        // block boundary.  |foo<br><p>bar| renders like |foo<p>bar| and
        // similarly |<p>foo<br></p>bar| renders like |<p>foo</p>bar|.  What
        // this <br> addition gets us is the ability to convert a trailing nbsp
        // to a space.  Consider: |<body>foo. '</body>|, where ' represents
        // selection.  User types space attempting to put 2 spaces after the
        // end of their sentence.  We used to do this as: |<body>foo.
        // &nbsp</body>|  This caused problems with soft wrapping: the nbsp
        // would wrap to the next line, which looked attrocious.  If you try to
        // do: |<body>foo.&nbsp </body>| instead, the trailing space is
        // invisible because it is against a block boundary.  If you do:
        // |<body>foo.&nbsp&nbsp</body>| then you get an even uglier soft
        // wrapping problem, where foo is on one line until you type the final
        // space, and then "foo  " jumps down to the next line.  Ugh.  The best
        // way I can find out of this is to throw in a harmless <br> here,
        // which allows us to do: |<body>foo.&nbsp <br></body>|, which doesn't
        // cause foo to jump lines, doesn't cause spaces to show up at the
        // beginning of soft wrapped lines, and lets the user see 2 spaces when
        // they type 2 spaces.

        RefPtr<Element> brElement =
            MOZ_KnownLive(mHTMLEditor)
                .InsertBRElementWithTransaction(aRun->EndPoint());
        if (NS_WARN_IF(!brElement)) {
          return NS_ERROR_FAILURE;
        }

        atPreviousCharOfEndOfRun = GetPreviousCharPoint(aRun->EndPoint());
        atPreviousCharOfPreviousCharOfEndOfRun =
            GetPreviousCharPointFromPointInText(atPreviousCharOfEndOfRun);
        rightCheck = true;
      }
    }
    if (leftCheck && rightCheck) {
      // Now replace nbsp with space.  First, insert a space
      AutoTransactionsConserveSelection dontChangeMySelection(mHTMLEditor);
      nsresult rv =
          MOZ_KnownLive(mHTMLEditor)
              .InsertTextIntoTextNodeWithTransaction(
                  NS_LITERAL_STRING(" "), atPreviousCharOfEndOfRun, true);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      // Finally, delete that nbsp
      NS_ASSERTION(!atPreviousCharOfEndOfRun.IsEndOfContainer() &&
                       !atPreviousCharOfEndOfRun.IsAtLastContent(),
                   "The text node was modified by mutation event listener");
      if (!atPreviousCharOfEndOfRun.IsEndOfContainer() &&
          !atPreviousCharOfEndOfRun.IsAtLastContent()) {
        NS_ASSERTION(atPreviousCharOfEndOfRun.IsNextCharNBSP(),
                     "Trying to remove an NBSP, but it's gone from the "
                     "expected position");
        EditorDOMPointInText atNextCharOfPreviousCharOfEndOfRun =
            atPreviousCharOfEndOfRun.NextPoint();
        nsresult rv =
            DeleteRange(atNextCharOfPreviousCharOfEndOfRun,
                        atNextCharOfPreviousCharOfEndOfRun.NextPoint());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    } else if (!mPRE && spaceNBSP && rightCheck) {
      // Don't mess with this preformatted for now.  We have a run of ASCII
      // whitespace (which will render as one space) followed by an nbsp (which
      // is at the end of the whitespace run).  Let's switch their order.  This
      // will ensure that if someone types two spaces after a sentence, and the
      // editor softwraps at this point, the spaces won't be split across lines,
      // which looks ugly and is bad for the moose.
      MOZ_ASSERT(!atPreviousCharOfPreviousCharOfEndOfRun.IsEndOfContainer());
      EditorDOMPointInText start, end;
      Tie(start, end) = GetASCIIWhitespacesBounds(
          eBoth, atPreviousCharOfPreviousCharOfEndOfRun.NextPoint());

      // Delete that nbsp
      NS_ASSERTION(!atPreviousCharOfEndOfRun.IsEndOfContainer(),
                   "The text node was modified by mutation event listener");
      if (!atPreviousCharOfEndOfRun.IsEndOfContainer()) {
        NS_ASSERTION(atPreviousCharOfEndOfRun.IsCharNBSP(),
                     "Trying to remove an NBSP, but it's gone from the "
                     "expected position");
        nsresult rv = DeleteRange(atPreviousCharOfEndOfRun,
                                  atPreviousCharOfEndOfRun.NextPoint());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }

      // Finally, insert that nbsp before the ASCII ws run
      NS_ASSERTION(start.IsSetAndValid(),
                   "The text node was modified by mutation event listener");
      if (start.IsSetAndValid()) {
        AutoTransactionsConserveSelection dontChangeMySelection(mHTMLEditor);
        nsresult rv = MOZ_KnownLive(mHTMLEditor)
                          .InsertTextIntoTextNodeWithTransaction(
                              nsDependentSubstring(&kNBSP, 1), start, true);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
  }
  return NS_OK;
}

nsresult WSRunObject::ReplacePreviousNBSPIfUnnecessary(
    WSFragment* aRun, const EditorDOMPoint& aPoint) {
  if (NS_WARN_IF(!aRun) || NS_WARN_IF(!aPoint.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }
  MOZ_ASSERT(aPoint.IsSetAndValid());

  // Try to change an NBSP to a space, if possible, just to prevent NBSP
  // proliferation.  This routine is called when we are about to make this
  // point in the ws abut an inserted break or text, so we don't have to worry
  // about what is after it.  What is after it now will end up after the
  // inserted object.
  bool canConvert = false;
  EditorDOMPointInText atPreviousChar = GetPreviousCharPoint(aPoint);
  if (atPreviousChar.IsSet() && !atPreviousChar.IsEndOfContainer() &&
      atPreviousChar.IsCharNBSP()) {
    EditorDOMPointInText atPreviousCharOfPreviousChar =
        GetPreviousCharPointFromPointInText(atPreviousChar);
    if (atPreviousCharOfPreviousChar.IsSet()) {
      if (atPreviousCharOfPreviousChar.IsEndOfContainer() ||
          !atPreviousCharOfPreviousChar.IsCharASCIISpace()) {
        // If previous character is a NBSP and its previous character isn't
        // ASCII space, we can replace the NBSP with ASCII space.
        canConvert = true;
      }
    } else if (aRun->mLeftType == WSType::text ||
               aRun->mLeftType == WSType::special) {
      // If previous character is a NBSP and it's the first character of the
      // text node, additionally, if its previous node is a text node including
      // non-whitespace characters or <img> node or something inline
      // non-container element node, we can replace the NBSP with ASCII space.
      canConvert = true;
    }
  }

  if (!canConvert) {
    return NS_OK;
  }

  // First, insert a space before the previous NBSP.
  AutoTransactionsConserveSelection dontChangeMySelection(mHTMLEditor);
  nsresult rv = MOZ_KnownLive(mHTMLEditor)
                    .InsertTextIntoTextNodeWithTransaction(
                        NS_LITERAL_STRING(" "), atPreviousChar, true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Finally, delete the previous NBSP.
  NS_ASSERTION(
      !atPreviousChar.IsEndOfContainer() && !atPreviousChar.IsAtLastContent(),
      "The text node was modified by mutation event listener");
  if (!atPreviousChar.IsEndOfContainer() && !atPreviousChar.IsAtLastContent()) {
    NS_ASSERTION(
        atPreviousChar.IsNextCharNBSP(),
        "Trying to remove an NBSP, but it's gone from the expected position");
    EditorDOMPointInText atNextCharOfPreviousChar = atPreviousChar.NextPoint();
    nsresult rv = DeleteRange(atNextCharOfPreviousChar,
                              atNextCharOfPreviousChar.NextPoint());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
    return rv;
  }

  return NS_OK;
}

nsresult WSRunObject::CheckLeadingNBSP(WSFragment* aRun, nsINode* aNode,
                                       int32_t aOffset) {
  // Try to change an nbsp to a space, if possible, just to prevent nbsp
  // proliferation This routine is called when we are about to make this point
  // in the ws abut an inserted text, so we don't have to worry about what is
  // before it.  What is before it now will end up before the inserted text.
  bool canConvert = false;
  EditorDOMPointInText atNextChar =
      GetNextCharPoint(EditorRawDOMPoint(aNode, aOffset));
  if (!atNextChar.IsSet() || NS_WARN_IF(atNextChar.IsEndOfContainer())) {
    return NS_OK;
  }

  if (atNextChar.IsCharNBSP()) {
    EditorDOMPointInText atNextCharOfNextCharOfNBSP =
        GetNextCharPointFromPointInText(atNextChar.NextPoint());
    if (atNextCharOfNextCharOfNBSP.IsSet()) {
      if (atNextCharOfNextCharOfNBSP.IsEndOfContainer() ||
          !atNextCharOfNextCharOfNBSP.IsCharASCIISpace()) {
        canConvert = true;
      }
    } else if (aRun->mRightType == WSType::text ||
               aRun->mRightType == WSType::special ||
               aRun->mRightType == WSType::br) {
      canConvert = true;
    }
  }
  if (canConvert) {
    // First, insert a space
    AutoTransactionsConserveSelection dontChangeMySelection(mHTMLEditor);
    nsresult rv = MOZ_KnownLive(mHTMLEditor)
                      .InsertTextIntoTextNodeWithTransaction(
                          NS_LITERAL_STRING(" "), atNextChar, true);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // Finally, delete that nbsp
    NS_ASSERTION(
        !atNextChar.IsEndOfContainer() && !atNextChar.IsAtLastContent(),
        "The text node was modified by mutation event listener");
    if (!atNextChar.IsEndOfContainer() && !atNextChar.IsAtLastContent()) {
      NS_ASSERTION(
          atNextChar.IsNextCharNBSP(),
          "Trying to remove an NBSP, but it's gone from the expected position");
      EditorDOMPointInText atNextCharOfNextChar = atNextChar.NextPoint();
      rv = DeleteRange(atNextCharOfNextChar, atNextCharOfNextChar.NextPoint());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "DeleteRange() failed");
      return rv;
    }
  }
  return NS_OK;
}

nsresult WSRunObject::Scrub() {
  WSFragment* run = mStartRun;
  while (run) {
    if (run->mType & (WSType::leadingWS | WSType::trailingWS)) {
      nsresult rv = DeleteRange(run->StartPoint(), run->EndPoint());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
    run = run->mRight;
  }
  return NS_OK;
}

}  // namespace mozilla
