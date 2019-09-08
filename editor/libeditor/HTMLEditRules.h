/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HTMLEditRules_h
#define HTMLEditRules_h

#include "TypeInState.h"
#include "mozilla/EditorDOMPoint.h"  // for EditorDOMPoint
#include "mozilla/SelectionState.h"
#include "mozilla/TextEditRules.h"
#include "mozilla/TypeInState.h"  // for AutoStyleCacheArray
#include "nsCOMPtr.h"
#include "nsIEditor.h"
#include "nsIHTMLEditor.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"
#include "nscore.h"

class nsAtom;
class nsINode;
class nsRange;

namespace mozilla {

class EditActionResult;
class HTMLEditor;
class SplitNodeResult;
class TextEditor;
enum class EditSubAction : int32_t;

namespace dom {
class Document;
class Element;
class Selection;
}  // namespace dom

/**
 * Same as TextEditRules, any methods which may modify the DOM tree or
 * Selection should be marked as MOZ_MUST_USE and return nsresult directly
 * or with simple class like EditActionResult.  And every caller of them
 * has to check whether the result is NS_ERROR_EDITOR_DESTROYED and if it is,
 * its callers should stop handling edit action since after mutation event
 * listener or selectionchange event listener disables the editor, we should
 * not modify the DOM tree nor Selection anymore.  And also when methods of
 * this class call methods of other classes like HTMLEditor and WSRunObject,
 * they should check whether CanHandleEditAtion() returns false immediately
 * after the calls.  If it returns false, they should return
 * NS_ERROR_EDITOR_DESTROYED.
 */

class HTMLEditRules : public TextEditRules {
 public:
  HTMLEditRules();

  // TextEditRules methods
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult Init(TextEditor* aTextEditor) override;
  virtual nsresult DetachEditor() override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual nsresult BeforeEdit() override;
  MOZ_CAN_RUN_SCRIPT virtual nsresult AfterEdit() override;
  // NOTE: Don't mark WillDoAction() nor DidDoAction() as MOZ_CAN_RUN_SCRIPT
  //       because they are too generic and doing it makes a lot of public
  //       editor methods marked as MOZ_CAN_RUN_SCRIPT too, but some of them
  //       may not causes running script.  So, ideal fix must be that we make
  //       each method callsed by this method public.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual nsresult WillDoAction(EditSubActionInfo& aInfo, bool* aCancel,
                                bool* aHandled) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual nsresult DidDoAction(EditSubActionInfo& aInfo,
                               nsresult aResult) override;
  virtual bool DocumentIsEmpty() const override;

  /**
   * DocumentModified() is called when editor content is changed.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult DocumentModified();

  MOZ_CAN_RUN_SCRIPT
  nsresult GetListState(bool* aMixed, bool* aOL, bool* aUL, bool* aDL);
  MOZ_CAN_RUN_SCRIPT
  nsresult GetListItemState(bool* aMixed, bool* aLI, bool* aDT, bool* aDD);
  MOZ_CAN_RUN_SCRIPT
  nsresult GetAlignment(bool* aMixed, nsIHTMLEditor::EAlignment* aAlign);
  MOZ_CAN_RUN_SCRIPT
  nsresult GetParagraphState(bool* aMixed, nsAString& outFormat);

  /**
   * MakeSureElemStartsAndEndsOnCR() inserts <br> element at start (and/or end)
   * of aNode if neither:
   * - first (last) editable child of aNode is a block or a <br>,
   * - previous (next) sibling of aNode is block or a <br>
   * - nor no previous (next) sibling of aNode.
   *
   * @param aNode               The node which may be inserted <br> elements.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult MakeSureElemStartsAndEndsOnCR(nsINode& aNode);

  void DidCreateNode(Element& aNewElement);
  void DidInsertNode(nsIContent& aNode);
  void WillDeleteNode(nsINode& aChild);
  void DidSplitNode(nsINode& aExistingRightNode, nsINode& aNewLeftNode);
  void WillJoinNodes(nsINode& aLeftNode, nsINode& aRightNode);
  void DidJoinNodes(nsINode& aLeftNode, nsINode& aRightNode);
  void DidInsertText(nsINode& aTextNode, int32_t aOffset,
                     const nsAString& aString);
  void DidDeleteText(nsINode& aTextNode, int32_t aOffset, int32_t aLength);
  void WillDeleteSelection();

 protected:
  virtual ~HTMLEditRules() = default;

  HTMLEditor& HTMLEditorRef() const {
    MOZ_ASSERT(mData);
    return mData->HTMLEditorRef();
  }

  /**
   * Called after deleting selected content.
   * This method removes unnecessary empty nodes and/or inserts <br> if
   * necessary.
   */
  MOZ_CAN_RUN_SCRIPT MOZ_MUST_USE nsresult DidDeleteSelection();

  /**
   * Called before indenting around Selection.  This method actually tries to
   * indent the contents.
   *
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillIndent(bool* aCancel, bool* aHandled);

  /**
   * Called before indenting around Selection and it's in CSS mode.
   * This method actually tries to indent the contents.
   *
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillCSSIndent(bool* aCancel, bool* aHandled);

  /**
   * Called before indenting around Selection and it's not in CSS mode.
   * This method actually tries to indent the contents.
   *
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillHTMLIndent(bool* aCancel, bool* aHandled);

  /**
   * Called before outdenting around Selection.  This method actually tries
   * to indent the contents.
   *
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillOutdent(bool* aCancel, bool* aHandled);

  /**
   * Called before aligning contents around Selection.  This method actually
   * sets align attributes to align contents.
   *
   * @param aAlignType          New align attribute value where the contents
   *                            should be aligned to.
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult WillAlign(const nsAString& aAlignType, bool* aCancel,
                     bool* aHandled);

  /**
   * Called before changing absolute positioned element to static positioned.
   * This method actually changes the position property of nearest absolute
   * positioned element.  Therefore, this might cause destroying the HTML
   * editor.
   *
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillRemoveAbsolutePosition(bool* aCancel,
                                                   bool* aHandled);

  /**
   * Called before changing z-index.
   * This method actually changes z-index of nearest absolute positioned
   * element relatively.  Therefore, this might cause destroying the HTML
   * editor.
   *
   * @param aChange             Amount to change z-index.
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillRelativeChangeZIndex(int32_t aChange, bool* aCancel,
                                                 bool* aHandled);

  /**
   * Called before changing an element to absolute positioned.
   * This method only prepares the operation since DidAbsolutePosition() will
   * change it actually later.  mNewBlockElement of TopLevelEditSubActionData
   * is set to the target element and if necessary, some ancestor nodes of
   * selection may be split.
   *
   * @param aCancel             Returns true if the operation is canceled.
   * @param aHandled            Returns true if the edit action is handled.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult WillAbsolutePosition(bool* aCancel, bool* aHandled);

  /**
   * PrepareToMakeElementAbsolutePosition() is helper method of
   * WillAbsolutePosition() since in some cases, needs to restore selection
   * with AutoSelectionRestorer.  So, all callers have to check if
   * CanHandleEditAction() still returns true after a call of this method.
   * XXX Should be documented outline of this method.
   *
   * @param aHandled            Returns true if the edit action is handled.
   * @param aTargetElement      Returns target element which should be
   *                            changed to absolute positioned.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult PrepareToMakeElementAbsolutePosition(
      bool* aHandled, RefPtr<Element>* aTargetElement);

  /**
   * Called if nobody handles the edit action to make an element absolute
   * positioned.
   * This method actually changes the element which is computed by
   * WillAbsolutePosition() to absolute positioned.
   * Therefore, this might cause destroying the HTML editor.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult DidAbsolutePosition();

  /**
   * AlignInnerBlocks() calls AlignBlockContents() for every list item element
   * and table cell element in aNode.
   *
   * @param aNode               The node whose descendants should be aligned
   *                            to aAlignType.
   * @param aAlignType          New value of align attribute of <div>.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult AlignInnerBlocks(nsINode& aNode,
                                         const nsAString& aAlignType);

  /**
   * AlignBlockContents() sets align attribute of <div> element which is
   * only child of aNode to aAlignType.  If aNode has 2 or more children or
   * does not have a <div> element has only child, inserts a <div> element
   * into aNode and move all children of aNode into the new <div> element.
   *
   * @param aNode               The node whose contents should be aligned
   *                            to aAlignType.
   * @param aAlignType          New value of align attribute of <div> which
   *                            is only child of aNode.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult AlignBlockContents(nsINode& aNode,
                                           const nsAString& aAlignType);

  /**
   * AlignContentsAtSelection() aligns contents around Selection to aAlignType.
   * This creates AutoSelectionRestorer.  Therefore, even if this returns
   * NS_OK, CanHandleEditAction() may return false if the editor is destroyed
   * during restoring the Selection.  So, every caller needs to check if
   * CanHandleEditAction() returns true before modifying the DOM tree or
   * changing Selection.
   *
   * @param aAlignType          New align attribute value where the contents
   *                            should be aligned to.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult AlignContentsAtSelection(const nsAString& aAlignType);

  nsresult AppendInnerFormatNodes(nsTArray<OwningNonNull<nsINode>>& aArray,
                                  nsINode* aNode);
  nsresult GetFormatString(nsINode* aNode, nsAString& outFormat);

  /**
   * Called after handling edit action.  This may adjust Selection, remove
   * unnecessary empty nodes, create <br> elements if needed, etc.
   */
  MOZ_CAN_RUN_SCRIPT MOZ_MUST_USE nsresult AfterEditInner();

  /**
   * IndentAroundSelectionWithHTML() indents around Selection with HTML.
   * This method creates AutoSelectionRestorer.  Therefore, each caller
   * need to check if the editor is still available even if this returns
   * NS_OK.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult IndentAroundSelectionWithHTML();

  /**
   * OutdentAroundSelection() outdents contents around Selection.
   * This method creates AutoSelectionRestorer.  Therefore, each caller
   * need to check if the editor is still available even if this returns
   * NS_OK.
   *
   * @return                    The left content is left content of last
   *                            outdented element.
   *                            The right content is right content of last
   *                            outdented element.
   *                            The middle content is middle content of last
   *                            outdented element.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE SplitRangeOffFromNodeResult OutdentAroundSelection();

  /**
   * OutdentPartOfBlock() outdents the nodes between aStartOfOutdent and
   * aEndOfOutdent.  This splits the range off from aBlockElement first.
   * Then, removes the middle element if aIsBlockIndentedWithCSS is false.
   * Otherwise, decreases the margin of the middle element.
   *
   * @param aBlockElement           A block element which includes both
   *                                aStartOfOutdent and aEndOfOutdent.
   * @param aStartOfOutdent         First node which is descendant of
   *                                aBlockElement will be outdented.
   * @param aEndOfOutdent           Last node which is descandant of
   *                                aBlockElement will be outdented.
   * @param aIsBlockIndentedWithCSS true if aBlockElement is indented with
   *                                CSS margin property.
   *                                false if aBlockElement is <blockquote>
   *                                or something.
   * @return                        The left content is new created element
   *                                splitting before aStartOfOutdent.
   *                                The right content is existing element.
   *                                The middle content is outdented element
   *                                if aIsBlockIndentedWithCSS is true.
   *                                Otherwise, nullptr.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE SplitRangeOffFromNodeResult
  OutdentPartOfBlock(Element& aBlockElement, nsIContent& aStartOfOutdent,
                     nsIContent& aEndOutdent, bool aIsBlockIndentedWithCSS);

  MOZ_CAN_RUN_SCRIPT
  nsresult GetParagraphFormatNodes(
      nsTArray<OwningNonNull<nsINode>>& outArrayOfNodes);

  /**
   * MakeTransitionList() detects all the transitions in the array, where a
   * transition means that adjacent nodes in the array don't have the same
   * parent.
   */
  void MakeTransitionList(nsTArray<OwningNonNull<nsINode>>& aNodeArray,
                          nsTArray<bool>& aTransitionArray);

  /**
   * InsertBRElementToEmptyListItemsAndTableCellsInRange() inserts
   * <br> element into empty list item or table cell elements between
   * aStartRef and aEndRef.
   */
  MOZ_CAN_RUN_SCRIPT MOZ_MUST_USE nsresult
  InsertBRElementToEmptyListItemsAndTableCellsInRange(
      const RawRangeBoundary& aStartRef, const RawRangeBoundary& aEndRef);

  /**
   * PinSelectionToNewBlock() may collapse Selection around mNewNode if it's
   * necessary,
   */
  MOZ_MUST_USE nsresult PinSelectionToNewBlock();

  void CheckInterlinePosition();

  /**
   * AdjustSelection() may adjust Selection range to nearest editable content.
   * Despite of the name, this may change the DOM tree.  If it needs to create
   * a <br> to put caret, this tries to create a <br> element.
   *
   * @param aAction     Maybe used to look for a good point to put caret.
   */
  MOZ_CAN_RUN_SCRIPT MOZ_MUST_USE nsresult
  AdjustSelection(nsIEditor::EDirection aAction);

  /**
   * FindNearEditableNode() tries to find an editable node near aPoint.
   *
   * @param aPoint      The DOM point where to start to search from.
   * @param aDirection  If nsIEditor::ePrevious is set, this searches an
   *                    editable node from next nodes.  Otherwise, from
   *                    previous nodes.
   * @return            If found, returns non-nullptr.  Otherwise, nullptr.
   *                    Note that if found node is in different table element,
   *                    this returns nullptr.
   *                    And also if aDirection is not nsIEditor::ePrevious,
   *                    the result may be the node pointed by aPoint.
   */
  template <typename PT, typename CT>
  nsIContent* FindNearEditableNode(const EditorDOMPointBase<PT, CT>& aPoint,
                                   nsIEditor::EDirection aDirection);

  /**
   * RemoveEmptyNodesInChangedRange() removes all empty nodes in
   * TopLevelEditSubActionData::mChangedRange.  However, if mail-cite node has
   * only a <br> element, the node will be removed but <br> element is moved
   * to where the mail-cite node was.
   * XXX This method is expensive if TopLevelEditSubActionData::mChangedRange
   *     is too wide and may remove unexpected empty element, e.g., it was
   *     created by JS, but we haven't touched it.  Cannot we remove this
   *     method and make guarantee that empty nodes won't be created?
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult RemoveEmptyNodesInChangedRange();

  nsresult SelectionEndpointInNode(nsINode* aNode, bool* aResult);

  /**
   * ConfirmSelectionInBody() makes sure that Selection is in editor root
   * element typically <body> element (see HTMLEditor::UpdateRootElement())
   * and only one Selection range.
   * XXX This method is not necessary because even if selection is outside the
   *     <body> element, elements outside the <body> element should be
   *     editable, e.g., any element can be inserted siblings as <body> element
   *     and other browsers allow to edit such elements.
   */
  MOZ_MUST_USE nsresult ConfirmSelectionInBody();

  /**
   * IsEmptyInline: Return true if aNode is an empty inline container
   */
  bool IsEmptyInline(nsINode& aNode);

  bool ListIsEmptyLine(nsTArray<OwningNonNull<nsINode>>& arrayOfNodes);

  /**
   * RemoveAlignment() removes align attributes, text-align properties and
   * <center> elements in aNode.
   *
   * @param aNode               Alignment information of the node and/or its
   *                            descendants will be removed.
   * @param aAlignType          New align value to be set only when it's in
   *                            CSS mode and this method meets <table> or <hr>.
   *                            XXX This is odd and not clear when you see
   *                                caller of this method.  Do you have better
   *                                idea?
   * @param aDescendantsOnly    true if align information of aNode itself
   *                            shouldn't be removed.  Otherwise, false.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult RemoveAlignment(nsINode& aNode,
                                        const nsAString& aAlignType,
                                        bool aDescendantsOnly);

  /**
   * MakeSureElemStartsOrEndsOnCR() inserts <br> element at start (end) of
   * aNode if neither:
   * - first (last) editable child of aNode is a block or a <br>,
   * - previous (next) sibling of aNode is block or a <br>
   * - nor no previous (next) sibling of aNode.
   *
   * @param aNode               The node which may be inserted <br> element.
   * @param aStarts             true for trying to insert <br> to the start.
   *                            false for trying to insert <br> to the end.
   */
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult MakeSureElemStartsOrEndsOnCR(nsINode& aNode,
                                                     bool aStarts);

  /**
   * AlignBlock() resets align attribute, text-align property, etc first.
   * Then, aligns contents of aElement on aAlignType.
   *
   * @param aElement            The element whose contents will be aligned.
   * @param aAlignType          Boundary or "center" which contents should be
   *                            aligned on.
   * @param aResetAlignOf       Resets align of whether element and its
   *                            descendants or only descendants.
   */
  enum class ResetAlignOf { ElementAndDescendants, OnlyDescendants };
  MOZ_CAN_RUN_SCRIPT
  MOZ_MUST_USE nsresult AlignBlock(Element& aElement,
                                   const nsAString& aAlignType,
                                   ResetAlignOf aResetAlignOf);

  /**
   * DocumentModifiedWorker() is called by DocumentModified() either
   * synchronously or asynchronously.
   */
  MOZ_CAN_RUN_SCRIPT void DocumentModifiedWorker();

 protected:
  HTMLEditor* mHTMLEditor;
  bool mInitialized;
};

}  // namespace mozilla

#endif  // #ifndef HTMLEditRules_h
