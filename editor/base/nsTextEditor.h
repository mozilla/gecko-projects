/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#ifndef nsTextEditor_h__
#define nsTextEditor_h__

#include "nsITextEditor.h"
#include "nsCOMPtr.h"
#include "nsIDOMEventListener.h"
#include "nsEditor.h"
#include "nsTextEditRules.h"
#include "TypeInState.h"

class nsIStyleContext;
class nsIDOMRange;

/**
 * The text editor implementation.<br>
 * Use to edit text represented as a DOM tree. 
 * This class is used for editing both plain text and rich text (attributed text).
 */
class nsTextEditor : public nsEditor, public nsITextEditor
{
public:
  // see nsITextEditor for documentation

//Interfaces for addref and release and queryinterface
//NOTE macro used is for classes that inherit from 
// another class. Only the base class should use NS_DECL_ISUPPORTS
  NS_DECL_ISUPPORTS_INHERITED

  nsTextEditor();
  virtual ~nsTextEditor();

//Initialization
  NS_IMETHOD Init(nsIDOMDocument *aDoc, nsIPresShell *aPresShell);

// Editing Operations
  NS_IMETHOD SetTextProperty(nsIAtom        *aProperty, 
                             const nsString *aAttribute,
                             const nsString *aValue);
  NS_IMETHOD GetTextProperty(nsIAtom *aProperty, 
                             const nsString *aAttribute,
                             const nsString *aValue,
                             PRBool &aFirst, PRBool &aAny, PRBool &aAll);
  NS_IMETHOD RemoveTextProperty(nsIAtom *aProperty, const nsString *aAttribute);
  NS_IMETHOD DeleteSelection(nsIEditor::Direction aDir);
  NS_IMETHOD InsertText(const nsString& aStringToInsert);
  NS_IMETHOD InsertBreak();

// Transaction control
  NS_IMETHOD EnableUndo(PRBool aEnable);
  NS_IMETHOD Undo(PRUint32 aCount);
  NS_IMETHOD CanUndo(PRBool &aIsEnabled, PRBool &aCanUndo);
  NS_IMETHOD Redo(PRUint32 aCount);
  NS_IMETHOD CanRedo(PRBool &aIsEnabled, PRBool &aCanRedo);
  NS_IMETHOD BeginTransaction();
  NS_IMETHOD EndTransaction();

// Selection and navigation -- exposed here for convenience
  NS_IMETHOD MoveSelectionUp(nsIAtom *aIncrement, PRBool aExtendSelection);
  NS_IMETHOD MoveSelectionDown(nsIAtom *aIncrement, PRBool aExtendSelection);
  NS_IMETHOD MoveSelectionNext(nsIAtom *aIncrement, PRBool aExtendSelection);
  NS_IMETHOD MoveSelectionPrevious(nsIAtom *aIncrement, PRBool aExtendSelection);
  NS_IMETHOD SelectNext(nsIAtom *aIncrement, PRBool aExtendSelection); 
  NS_IMETHOD SelectPrevious(nsIAtom *aIncrement, PRBool aExtendSelection);
  NS_IMETHOD SelectAll();
  NS_IMETHOD ScrollUp(nsIAtom *aIncrement);
  NS_IMETHOD ScrollDown(nsIAtom *aIncrement);
  NS_IMETHOD ScrollIntoView(PRBool aScrollToBegin);

// cut, copy & paste
  NS_IMETHOD Cut();
  NS_IMETHOD Copy();
  NS_IMETHOD Paste();

// Input/Output
  NS_IMETHOD Insert(nsString& aInputString);
  NS_IMETHOD OutputText(nsString& aOutputString);
  NS_IMETHOD OutputHTML(nsString& aOutputString);


protected:

// rules initialization

  virtual void  InitRules();
  
// Utility Methods

  virtual void IsTextPropertySetByContent(nsIDOMNode  *aNode,
                                          nsIAtom     *aProperty, 
                                          const nsString *aAttribute,
                                          const nsString *aValue,
                                          PRBool      &aIsSet,
                                          nsIDOMNode **aStyleNode) const;

  virtual void IsTextStyleSet(nsIStyleContext *aSC, 
                              nsIAtom         *aProperty, 
                              const nsString  *aAttributes, 
                              PRBool          &aIsSet) const;

  NS_IMETHOD IsNodeInline(nsIDOMNode *aNode, PRBool &aIsInline) const;

  NS_IMETHOD IntermediateNodesAreInline(nsIDOMRange  *aRange,
                                        nsIDOMNode   *aStartNode, 
                                        PRInt32       aStartOffset, 
                                        nsIDOMNode   *aEndNode,
                                        PRInt32       aEndOffset,
                                        nsIDOMNode   *aParent,
                                        PRBool       &aResult) const;

  NS_IMETHOD SetTextPropertiesForNode(nsIDOMNode  *aNode, 
                                      nsIDOMNode  *aParent,
                                      PRInt32      aStartOffset,
                                      PRInt32      aEndOffset,
                                      nsIAtom     *aPropName,
                                      const nsString *aAttribute,
                                      const nsString *aValue);

  NS_IMETHOD SetTextPropertiesForNodesWithSameParent(nsIDOMNode  *aStartNode,
                                                     PRInt32      aStartOffset,
                                                     nsIDOMNode  *aEndNode,
                                                     PRInt32      aEndOffset,
                                                     nsIDOMNode  *aParent,
                                                     nsIAtom     *aPropName,
                                                     const nsString *aAttribute,
                                                     const nsString *aValue);

  NS_IMETHOD SetTextPropertiesForNodeWithDifferentParents(nsIDOMRange *aRange,
                                                          nsIDOMNode  *aStartNode,
                                                          PRInt32      aStartOffset,
                                                          nsIDOMNode  *aEndNode,
                                                          PRInt32      aEndOffset,
                                                          nsIDOMNode  *aParent,
                                                          nsIAtom     *aPropName,
                                                          const nsString *aAttribute,
                                                          const nsString *aValue);

  NS_IMETHOD RemoveTextPropertiesForNode(nsIDOMNode  *aNode, 
                                         nsIDOMNode  *aParent,
                                         PRInt32      aStartOffset,
                                         PRInt32      aEndOffset,
                                         nsIAtom     *aPropName,
                                         const nsString *aAttribute);

  NS_IMETHOD RemoveTextPropertiesForNodesWithSameParent(nsIDOMNode  *aStartNode,
                                                        PRInt32      aStartOffset,
                                                        nsIDOMNode  *aEndNode,
                                                        PRInt32      aEndOffset,
                                                        nsIDOMNode  *aParent,
                                                        nsIAtom     *aPropName, 
                                                        const nsString *aAttribute);

  NS_IMETHOD RemoveTextPropertiesForNodeWithDifferentParents(nsIDOMRange *aRange,
                                                             nsIDOMNode  *aStartNode,
                                                             PRInt32      aStartOffset,
                                                             nsIDOMNode  *aEndNode,
                                                             PRInt32      aEndOffset,
                                                             nsIDOMNode  *aParent,
                                                             nsIAtom     *aPropName,
                                                             const nsString *aAttribute);



  NS_IMETHOD SetTypeInStateForProperty(TypeInState &aTypeInState, 
                                       nsIAtom     *aPropName, 
                                       const nsString *aAttribute);
  
  TypeInState GetTypeInState() { return mTypeInState;}


// Data members
protected:
  TypeInState      mTypeInState;  // xxx - isn't it wrong to have xpcom classes as members?  shouldn't it be a pointer?
  nsTextEditRules* mRules;
  nsCOMPtr<nsIDOMEventListener> mKeyListenerP;
  nsCOMPtr<nsIDOMEventListener> mMouseListenerP;
  nsCOMPtr<nsIDOMEventListener> mTextListenerP;
  nsCOMPtr<nsIDOMEventListener> mDragListenerP;

// friends
friend class nsTextEditRules;
};

#endif //nsTextEditor_h__

