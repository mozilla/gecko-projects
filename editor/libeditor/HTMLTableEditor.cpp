/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

#include "mozilla/HTMLEditor.h"

#include "HTMLEditUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/EditAction.h"
#include "mozilla/EditorDOMPoint.h"
#include "mozilla/EditorUtils.h"
#include "mozilla/FlushType.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Element.h"
#include "nsAString.h"
#include "nsAlgorithm.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsAtom.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsIPresShell.h"
#include "nsISupportsUtils.h"
#include "nsITableCellLayout.h" // For efficient access to table cell
#include "nsLiteralString.h"
#include "nsQueryFrame.h"
#include "nsRange.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTableCellFrame.h"
#include "nsTableWrapperFrame.h"
#include "nscore.h"
#include <algorithm>

namespace mozilla {

using namespace dom;

/**
 * Stack based helper class for restoring selection after table edit.
 */
class MOZ_STACK_CLASS AutoSelectionSetterAfterTableEdit final
{
private:
  RefPtr<HTMLEditor> mHTMLEditor;
  RefPtr<Element> mTable;
  int32_t mCol, mRow, mDirection, mSelected;

public:
  AutoSelectionSetterAfterTableEdit(HTMLEditor& aHTMLEditor,
                                    Element* aTable,
                                    int32_t aRow,
                                    int32_t aCol,
                                    int32_t aDirection,
                                    bool aSelected)
    : mHTMLEditor(&aHTMLEditor)
    , mTable(aTable)
    , mCol(aCol)
    , mRow(aRow)
    , mDirection(aDirection)
    , mSelected(aSelected)
  {
  }

  ~AutoSelectionSetterAfterTableEdit()
  {
    if (mHTMLEditor) {
      mHTMLEditor->SetSelectionAfterTableEdit(mTable, mRow, mCol, mDirection,
                                              mSelected);
    }
  }

  // This is needed to abort the caret reset in the destructor
  //  when one method yields control to another
  void CancelSetCaret()
  {
    mHTMLEditor = nullptr;
    mTable = nullptr;
  }
};

nsresult
HTMLEditor::InsertCell(Element* aCell,
                       int32_t aRowSpan,
                       int32_t aColSpan,
                       bool aAfter,
                       bool aIsHeader,
                       Element** aNewCell)
{
  if (aNewCell) {
    *aNewCell = nullptr;
  }

  if (NS_WARN_IF(!aCell)) {
    return NS_ERROR_NULL_POINTER;
  }

  // And the parent and offsets needed to do an insert
  EditorDOMPoint pointToInsert(aCell);
  if (NS_WARN_IF(!pointToInsert.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<Element> newCell =
    CreateElementWithDefaults(aIsHeader ? *nsGkAtoms::th : *nsGkAtoms::td);
  if (NS_WARN_IF(!newCell)) {
    return NS_ERROR_FAILURE;
  }

  //Optional: return new cell created
  if (aNewCell) {
    *aNewCell = do_AddRef(newCell).take();
  }

  if (aRowSpan > 1) {
    // Note: Do NOT use editor transaction for this
    nsAutoString newRowSpan;
    newRowSpan.AppendInt(aRowSpan, 10);
    newCell->SetAttr(kNameSpaceID_None, nsGkAtoms::rowspan, newRowSpan, true);
  }
  if (aColSpan > 1) {
    // Note: Do NOT use editor transaction for this
    nsAutoString newColSpan;
    newColSpan.AppendInt(aColSpan, 10);
    newCell->SetAttr(kNameSpaceID_None, nsGkAtoms::colspan, newColSpan, true);
  }
  if (aAfter) {
    DebugOnly<bool> advanced = pointToInsert.AdvanceOffset();
    NS_WARNING_ASSERTION(advanced,
      "Failed to advance offset to after the old cell");
  }

  // Don't let Rules System change the selection.
  AutoTransactionsConserveSelection dontChangeSelection(*this);
  return InsertNodeWithTransaction(*newCell, pointToInsert);
}

nsresult
HTMLEditor::SetColSpan(Element* aCell,
                       int32_t aColSpan)
{
  if (NS_WARN_IF(!aCell)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoString newSpan;
  newSpan.AppendInt(aColSpan, 10);
  return SetAttributeWithTransaction(*aCell, *nsGkAtoms::colspan, newSpan);
}

nsresult
HTMLEditor::SetRowSpan(Element* aCell,
                       int32_t aRowSpan)
{
  if (NS_WARN_IF(!aCell)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoString newSpan;
  newSpan.AppendInt(aRowSpan, 10);
  return SetAttributeWithTransaction(*aCell, *nsGkAtoms::rowspan, newSpan);
}

NS_IMETHODIMP
HTMLEditor::InsertTableCell(int32_t aNumber,
                            bool aAfter)
{
  RefPtr<Element> table;
  RefPtr<Element> curCell;
  nsCOMPtr<nsINode> cellParent;
  int32_t cellOffset, startRowIndex, startColIndex;
  nsresult rv = GetCellContext(nullptr,
                               getter_AddRefs(table),
                               getter_AddRefs(curCell),
                               getter_AddRefs(cellParent), &cellOffset,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  // Don't fail if no cell found
  NS_ENSURE_TRUE(curCell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

  // Get more data for current cell in row we are inserting at (we need COLSPAN)
  int32_t curStartRowIndex, curStartColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  rv = GetCellDataAt(table, startRowIndex, startColIndex,
                     getter_AddRefs(curCell),
                     &curStartRowIndex, &curStartColIndex, &rowSpan, &colSpan,
                     &actualRowSpan, &actualColSpan, &isSelected);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(curCell, NS_ERROR_FAILURE);
  int32_t newCellIndex = aAfter ? (startColIndex+colSpan) : startColIndex;
  //We control selection resetting after the insert...
  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             newCellIndex, ePreviousColumn,
                                             false);
  //...so suppress Rules System selection munging
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  for (int32_t i = 0; i < aNumber; i++) {
    RefPtr<Element> newCell = CreateElementWithDefaults(*nsGkAtoms::td);
    if (newCell) {
      if (aAfter) {
        cellOffset++;
      }
      rv = InsertNodeWithTransaction(*newCell,
                                     EditorRawDOMPoint(cellParent, cellOffset));
      if (NS_FAILED(rv)) {
        break;
      }
    } else {
      rv = NS_ERROR_FAILURE;
    }
  }
  // XXX This is perhaps the result of the last call of
  //     InsertNodeWithTransaction() or CreateElementWithDefaults().
  return rv;
}

NS_IMETHODIMP
HTMLEditor::GetFirstRow(Element* aTableOrElementInTable,
                        Element** aFirstRowElement)
{
  if (NS_WARN_IF(!aTableOrElementInTable) || NS_WARN_IF(!aFirstRowElement)) {
    return NS_ERROR_INVALID_ARG;
  }
  ErrorResult error;
  RefPtr<Element> firstRowElement =
    GetFirstTableRowElement(*aTableOrElementInTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  firstRowElement.forget(aFirstRowElement);
  return NS_OK;
}

Element*
HTMLEditor::GetFirstTableRowElement(Element& aTableOrElementInTable,
                                    ErrorResult& aRv) const
{
  MOZ_ASSERT(!aRv.Failed());

  Element* tableElement =
    GetElementOrParentByTagNameInternal(*nsGkAtoms::table,
                                        aTableOrElementInTable);
  // If the element is not in <table>, return error.
  if (NS_WARN_IF(!tableElement)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  for (nsIContent* tableChild = tableElement->GetFirstChild();
       tableChild;
       tableChild = tableChild->GetNextSibling()) {
    if (tableChild->IsHTMLElement(nsGkAtoms::tr)) {
      // Found a row directly under <table>
      return tableChild->AsElement();
    }
    // <table> can have table section elements like <tbody>.  <tr> elements
    // may be children of them.
    if (tableChild->IsAnyOfHTMLElements(nsGkAtoms::tbody,
                                        nsGkAtoms::thead,
                                        nsGkAtoms::tfoot)) {
      for (nsIContent* tableSectionChild = tableChild->GetFirstChild();
           tableSectionChild;
           tableSectionChild = tableSectionChild->GetNextSibling()) {
        if (tableSectionChild->IsHTMLElement(nsGkAtoms::tr)) {
          return tableSectionChild->AsElement();
        }
      }
    }
  }
  // Don't return error when there is no <tr> element in the <table>.
  return nullptr;
}

Element*
HTMLEditor::GetNextTableRowElement(Element& aTableRowElement,
                                   ErrorResult& aRv) const
{
  MOZ_ASSERT(!aRv.Failed());

  if (NS_WARN_IF(!aTableRowElement.IsHTMLElement(nsGkAtoms::tr))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  for (nsIContent* maybeNextRow = aTableRowElement.GetNextSibling();
       maybeNextRow;
       maybeNextRow = maybeNextRow->GetNextSibling()) {
    if (maybeNextRow->IsHTMLElement(nsGkAtoms::tr)) {
      return maybeNextRow->AsElement();
    }
  }

  // In current table section (e.g., <tbody>), there is no <tr> element.
  // Then, check the following table sections.
  Element* parentElementOfRow = aTableRowElement.GetParentElement();
  if (NS_WARN_IF(!parentElementOfRow)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Basically, <tr> elements should be in table section elements even if
  // they are not written in the source explicitly.  However, for preventing
  // cross table boundary, check it now.
  if (parentElementOfRow->IsHTMLElement(nsGkAtoms::table)) {
    // Don't return error since this means just not found.
    return nullptr;
  }

  for (nsIContent* maybeNextTableSection = parentElementOfRow->GetNextSibling();
       maybeNextTableSection;
       maybeNextTableSection = maybeNextTableSection->GetNextSibling()) {
    // If the sibling of parent of given <tr> is a table section element,
    // check its children.
    if (maybeNextTableSection->IsAnyOfHTMLElements(nsGkAtoms::tbody,
                                                   nsGkAtoms::thead,
                                                   nsGkAtoms::tfoot)) {
      for (nsIContent* maybeNextRow = maybeNextTableSection->GetFirstChild();
           maybeNextRow;
           maybeNextRow = maybeNextRow->GetNextSibling()) {
        if (maybeNextRow->IsHTMLElement(nsGkAtoms::tr)) {
          return maybeNextRow->AsElement();
        }
      }
    }
    // I'm not sure whether this is a possible case since table section
    // elements are created automatically.  However, DOM API may create
    // <tr> elements without table section elements.  So, let's check it.
    else if (maybeNextTableSection->IsHTMLElement(nsGkAtoms::tr)) {
      return maybeNextTableSection->AsElement();
    }
  }
  // Don't return error when the given <tr> element is the last <tr> element in
  // the <table>.
  return nullptr;
}

nsresult
HTMLEditor::GetLastCellInRow(nsINode* aRowNode,
                             nsINode** aCellNode)
{
  NS_ENSURE_TRUE(aCellNode, NS_ERROR_NULL_POINTER);

  *aCellNode = nullptr;

  NS_ENSURE_TRUE(aRowNode, NS_ERROR_NULL_POINTER);

  nsCOMPtr<nsINode> rowChild = aRowNode->GetLastChild();

  while (rowChild && !HTMLEditUtils::IsTableCell(rowChild)) {
    // Skip over textnodes
    rowChild = rowChild->GetPreviousSibling();
  }
  if (rowChild) {
    rowChild.forget(aCellNode);
    return NS_OK;
  }
  // If here, cell was not found
  return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
}

NS_IMETHODIMP
HTMLEditor::InsertTableColumn(int32_t aNumber,
                              bool aAfter)
{
  RefPtr<Selection> selection;
  RefPtr<Element> table;
  RefPtr<Element> curCell;
  int32_t startRowIndex, startColIndex;
  nsresult rv = GetCellContext(getter_AddRefs(selection),
                               getter_AddRefs(table),
                               getter_AddRefs(curCell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  // Don't fail if no cell found
  NS_ENSURE_TRUE(curCell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

  // Get more data for current cell (we need ROWSPAN)
  int32_t curStartRowIndex, curStartColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  rv = GetCellDataAt(table, startRowIndex, startColIndex,
                     getter_AddRefs(curCell),
                     &curStartRowIndex, &curStartColIndex,
                     &rowSpan, &colSpan,
                     &actualRowSpan, &actualColSpan, &isSelected);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(curCell, NS_ERROR_FAILURE);

  AutoPlaceholderBatch beginBatching(this);
  // Prevent auto insertion of BR in new cell until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eInsertNode,
                                      nsIEditor::eNext);

  // Use column after current cell if requested
  if (aAfter) {
    startColIndex += actualColSpan;
    //Detect when user is adding after a COLSPAN=0 case
    // Assume they want to stop the "0" behavior and
    // really add a new column. Thus we set the
    // colspan to its true value
    if (!colSpan) {
      SetColSpan(curCell, actualColSpan);
    }
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  //We reset caret in destructor...
  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             startColIndex, ePreviousRow,
                                             false);
  //.. so suppress Rules System selection munging
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  // If we are inserting after all existing columns
  // Make sure table is "well formed"
  //  before appending new column
  if (startColIndex >= tableSize.mColumnCount) {
    NormalizeTable(table);
  }

  RefPtr<Element> rowElement;
  for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
    if (startColIndex < tableSize.mColumnCount) {
      // We are inserting before an existing column
      rv = GetCellDataAt(table, rowIndex, startColIndex,
                         getter_AddRefs(curCell),
                         &curStartRowIndex, &curStartColIndex,
                         &rowSpan, &colSpan,
                         &actualRowSpan, &actualColSpan, &isSelected);
      NS_ENSURE_SUCCESS(rv, rv);

      // Don't fail entire process if we fail to find a cell
      //  (may fail just in particular rows with < adequate cells per row)
      if (curCell) {
        if (curStartColIndex < startColIndex) {
          // We have a cell spanning this location
          // Simply increase its colspan to keep table rectangular
          // Note: we do nothing if colsSpan=0,
          //  since it should automatically span the new column
          if (colSpan > 0) {
            SetColSpan(curCell, colSpan+aNumber);
          }
        } else {
          // Simply set selection to the current cell
          //  so we can let InsertTableCell() do the work
          // Insert a new cell before current one
          selection->Collapse(curCell, 0);
          rv = InsertTableCell(aNumber, false);
        }
      }
    } else {
      // Get current row and append new cells after last cell in row
      if (!rowIndex) {
        rowElement = GetFirstTableRowElement(*table, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
      } else {
        if (NS_WARN_IF(!rowElement)) {
          // XXX Looks like that when rowIndex is 0, startColIndex is always
          //     same as or larger than tableSize.mColumnCount.  Is it true?
          return NS_ERROR_FAILURE;
        }
        rowElement = GetNextTableRowElement(*rowElement, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
      }

      if (rowElement) {
        nsCOMPtr<nsINode> lastCell;
        rv = GetLastCellInRow(rowElement, getter_AddRefs(lastCell));
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
        if (NS_WARN_IF(!lastCell)) {
          return NS_ERROR_FAILURE;
        }

        curCell = lastCell->AsElement();
        // Simply add same number of cells to each row
        // Although tempted to check cell indexes for curCell,
        //  the effects of COLSPAN>1 in some cells makes this futile!
        // We must use NormalizeTable first to assure
        //  that there are cells in each cellmap location
        selection->Collapse(curCell, 0);
        rv = InsertTableCell(aNumber, true);
      }
    }
  }
  // XXX This is perhaps the result of the last call of InsertTableCell().
  return rv;
}

NS_IMETHODIMP
HTMLEditor::InsertTableRow(int32_t aNumber,
                           bool aAfter)
{
  RefPtr<Element> table;
  RefPtr<Element> curCell;

  int32_t startRowIndex, startColIndex;
  nsresult rv = GetCellContext(nullptr,
                               getter_AddRefs(table),
                               getter_AddRefs(curCell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  // Don't fail if no cell found
  NS_ENSURE_TRUE(curCell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

  // Get more data for current cell in row we are inserting at (we need COLSPAN)
  int32_t curStartRowIndex, curStartColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  rv = GetCellDataAt(table, startRowIndex, startColIndex,
                     getter_AddRefs(curCell),
                     &curStartRowIndex, &curStartColIndex,
                     &rowSpan, &colSpan,
                     &actualRowSpan, &actualColSpan, &isSelected);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(curCell, NS_ERROR_FAILURE);

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  AutoPlaceholderBatch beginBatching(this);
  // Prevent auto insertion of BR in new cell until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eInsertNode,
                                      nsIEditor::eNext);

  if (aAfter) {
    // Use row after current cell
    startRowIndex += actualRowSpan;

    //Detect when user is adding after a ROWSPAN=0 case
    // Assume they want to stop the "0" behavior and
    // really add a new row. Thus we set the
    // rowspan to its true value
    if (!rowSpan) {
      SetRowSpan(curCell, actualRowSpan);
    }
  }

  //We control selection resetting after the insert...
  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             startColIndex, ePreviousColumn,
                                             false);
  //...so suppress Rules System selection munging
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  RefPtr<Element> cellForRowParent;
  int32_t cellsInRow = 0;
  if (startRowIndex < tableSize.mRowCount) {
    // We are inserting above an existing row
    // Get each cell in the insert row to adjust for COLSPAN effects while we
    //   count how many cells are needed
    int32_t colIndex = 0;
    while (NS_SUCCEEDED(GetCellDataAt(table, startRowIndex, colIndex,
                                      getter_AddRefs(curCell),
                                      &curStartRowIndex, &curStartColIndex,
                                      &rowSpan, &colSpan,
                                      &actualRowSpan, &actualColSpan,
                                      &isSelected))) {
      if (curCell) {
        if (curStartRowIndex < startRowIndex) {
          // We have a cell spanning this location
          // Simply increase its rowspan
          //Note that if rowSpan == 0, we do nothing,
          //  since that cell should automatically extend into the new row
          if (rowSpan > 0) {
            SetRowSpan(curCell, rowSpan+aNumber);
          }
        } else {
          // We have a cell in the insert row

          // Count the number of cells we need to add to the new row
          cellsInRow += actualColSpan;

          // Save cell we will use below
          if (!cellForRowParent) {
            cellForRowParent = curCell;
          }
        }
        // Next cell in row
        colIndex += actualColSpan;
      } else {
        colIndex++;
      }
    }
  } else {
    // We are adding a new row after all others
    // If it weren't for colspan=0 effect,
    // we could simply use tableSize.mColumnCount for number of new cells...
    // XXX colspan=0 support has now been removed in table layout so maybe this can be cleaned up now? (bug 1243183)
    cellsInRow = tableSize.mColumnCount;

    // ...but we must compensate for all cells with rowSpan = 0 in the last row
    int32_t lastRow = tableSize.mRowCount - 1;
    int32_t tempColIndex = 0;
    while (NS_SUCCEEDED(GetCellDataAt(table, lastRow, tempColIndex,
                                      getter_AddRefs(curCell),
                                      &curStartRowIndex, &curStartColIndex,
                                      &rowSpan, &colSpan,
                                      &actualRowSpan, &actualColSpan,
                                      &isSelected))) {
      if (!rowSpan) {
        cellsInRow -= actualColSpan;
      }

      tempColIndex += actualColSpan;

      // Save cell from the last row that we will use below
      if (!cellForRowParent && curStartRowIndex == lastRow) {
        cellForRowParent = curCell;
      }
    }
  }

  if (cellsInRow > 0) {
    if (NS_WARN_IF(!cellForRowParent)) {
      return NS_ERROR_FAILURE;
    }
    Element* parentRow =
      GetElementOrParentByTagNameInternal(*nsGkAtoms::tr, *cellForRowParent);
    if (NS_WARN_IF(!parentRow)) {
      return NS_ERROR_FAILURE;
    }

    // The row parent and offset where we will insert new row
    nsCOMPtr<nsINode> parentOfRow = parentRow->GetParentNode();
    if (NS_WARN_IF(!parentOfRow)) {
      return NS_ERROR_FAILURE;
    }
    int32_t newRowOffset = parentOfRow->ComputeIndexOf(parentRow);

    // Adjust for when adding past the end
    if (aAfter && startRowIndex >= tableSize.mRowCount) {
      newRowOffset++;
    }

    for (int32_t row = 0; row < aNumber; row++) {
      // Create a new row
      RefPtr<Element> newRow = CreateElementWithDefaults(*nsGkAtoms::tr);
      if (NS_WARN_IF(!newRow)) {
        return NS_ERROR_FAILURE;
      }

      for (int32_t i = 0; i < cellsInRow; i++) {
        RefPtr<Element> newCell = CreateElementWithDefaults(*nsGkAtoms::td);
        if (NS_WARN_IF(!newCell)) {
          return NS_ERROR_FAILURE;
        }

        // Don't use transaction system yet! (not until entire row is
        // inserted)
        newRow->AppendChild(*newCell, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
      }

      // Use transaction system to insert the entire row+cells
      // (Note that rows are inserted at same childoffset each time)
      rv = InsertNodeWithTransaction(*newRow,
                                     EditorRawDOMPoint(parentOfRow,
                                                       newRowOffset));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  // SetSelectionAfterTableEdit from AutoSelectionSetterAfterTableEdit will
  // access frame selection, so we need reframe.
  // Because GetTableCellElementAt() depends on frame.
  nsCOMPtr<nsIPresShell> ps = GetPresShell();
  if (ps) {
    ps->FlushPendingNotifications(FlushType::Frames);
  }

  return NS_OK;
}

// Editor helper only
// XXX Code changed for bug 217717 and now we don't need aSelection param
//     TODO: Remove aSelection param
nsresult
HTMLEditor::DeleteTable2(Element* aTable,
                         Selection* aSelection)
{
  NS_ENSURE_TRUE(aTable, NS_ERROR_NULL_POINTER);

  // Select the table
  nsresult rv = ClearSelection();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  rv = AppendNodeToSelectionAsRange(aTable);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = DeleteSelectionAsSubAction(eNext, eStrip);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::DeleteTable()
{
  RefPtr<Selection> selection;
  RefPtr<Element> table;
  nsresult rv = GetCellContext(getter_AddRefs(selection),
                               getter_AddRefs(table),
                               nullptr, nullptr, nullptr, nullptr, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  AutoPlaceholderBatch beginBatching(this);
  return DeleteTable2(table, selection);
}

NS_IMETHODIMP
HTMLEditor::DeleteTableCell(int32_t aNumber)
{
  RefPtr<Selection> selection;
  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;


  nsresult rv = GetCellContext(getter_AddRefs(selection),
                               getter_AddRefs(table),
                               getter_AddRefs(cell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (NS_WARN_IF(!table) || NS_WARN_IF(!cell)) {
    // Don't fail if we didn't find a table or cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  AutoPlaceholderBatch beginBatching(this);
  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);

  RefPtr<Element> firstCell;
  rv = GetFirstSelectedCell(nullptr, getter_AddRefs(firstCell));
  NS_ENSURE_SUCCESS(rv, rv);

  // When 2 or more cells are selected, ignore aNumber and use selected cells.
  if (firstCell && selection->RangeCount() > 1) {
    ErrorResult error;
    TableSize tableSize(*this, *table, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }

    CellIndexes firstCellIndexes(*firstCell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    cell = firstCell;
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;

    // The setCaret object will call AutoSelectionSetterAfterTableEdit in its
    // destructor
    AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                               startColIndex, ePreviousColumn,
                                               false);
    AutoTransactionsConserveSelection dontChangeSelection(*this);

    bool    checkToDeleteRow = true;
    bool    checkToDeleteColumn = true;
    while (cell) {
      bool deleteRow = false;
      bool deleteCol = false;

      if (checkToDeleteRow) {
        // Optimize to delete an entire row
        // Clear so we don't repeat AllCellsInRowSelected within the same row
        checkToDeleteRow = false;

        deleteRow =
          AllCellsInRowSelected(table, startRowIndex, tableSize.mColumnCount);
        if (deleteRow) {
          // First, find the next cell in a different row
          //   to continue after we delete this row
          int32_t nextRow = startRowIndex;
          while (nextRow == startRowIndex) {
            rv = GetNextSelectedCell(nullptr, getter_AddRefs(cell));
            NS_ENSURE_SUCCESS(rv, rv);
            if (!cell) {
              break;
            }
            CellIndexes nextSelectedCellIndexes(*cell, error);
            if (NS_WARN_IF(error.Failed())) {
              return error.StealNSResult();
            }
            nextRow = nextSelectedCellIndexes.mRow;
            startColIndex = nextSelectedCellIndexes.mColumn;
          }
          // Delete entire row
          rv = DeleteRow(table, startRowIndex);
          NS_ENSURE_SUCCESS(rv, rv);

          if (cell) {
            // For the next cell: Subtract 1 for row we deleted
            startRowIndex = nextRow - 1;
            // Set true since we know we will look at a new row next
            checkToDeleteRow = true;
          }
        }
      }
      if (!deleteRow) {
        if (checkToDeleteColumn) {
          // Optimize to delete an entire column
          // Clear this so we don't repeat AllCellsInColSelected within the same Col
          checkToDeleteColumn = false;

          deleteCol =
            AllCellsInColumnSelected(table, startColIndex,
                                     tableSize.mColumnCount);
          if (deleteCol) {
            // First, find the next cell in a different column
            //   to continue after we delete this column
            int32_t nextCol = startColIndex;
            while (nextCol == startColIndex) {
              rv = GetNextSelectedCell(nullptr, getter_AddRefs(cell));
              NS_ENSURE_SUCCESS(rv, rv);
              if (!cell) {
                break;
              }
              CellIndexes nextSelectedCellIndexes(*cell, error);
              if (NS_WARN_IF(error.Failed())) {
                return error.StealNSResult();
              }
              startRowIndex = nextSelectedCellIndexes.mRow;
              nextCol = nextSelectedCellIndexes.mColumn;
            }
            // Delete entire Col
            rv = DeleteColumn(table, startColIndex);
            NS_ENSURE_SUCCESS(rv, rv);
            if (cell) {
              // For the next cell, subtract 1 for col. deleted
              startColIndex = nextCol - 1;
              // Set true since we know we will look at a new column next
              checkToDeleteColumn = true;
            }
          }
        }
        if (!deleteCol) {
          // First get the next cell to delete
          RefPtr<Element> nextCell;
          rv = GetNextSelectedCell(nullptr, getter_AddRefs(nextCell));
          NS_ENSURE_SUCCESS(rv, rv);

          // Then delete the cell
          rv = DeleteNodeWithTransaction(*cell);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return rv;
          }

          // The next cell to delete
          if (nextCell) {
            CellIndexes nextCellIndexes(*nextCell, error);
            if (NS_WARN_IF(error.Failed())) {
              return error.StealNSResult();
            }
            startRowIndex = nextCellIndexes.mRow;
            startColIndex = nextCellIndexes.mColumn;
          }
          cell = nextCell;
        }
      }
    }
  } else {
    ErrorResult error;
    for (int32_t i = 0; i < aNumber; i++) {
      rv = GetCellContext(getter_AddRefs(selection),
                          getter_AddRefs(table),
                          getter_AddRefs(cell),
                          nullptr, nullptr,
                          &startRowIndex, &startColIndex);
      NS_ENSURE_SUCCESS(rv, rv);
      // Don't fail if no cell found
      if (NS_WARN_IF(!cell)) {
        return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
      }

      if (GetNumberOfCellsInRow(table, startRowIndex) == 1) {
        Element* parentRow =
          GetElementOrParentByTagNameInternal(*nsGkAtoms::tr, *cell);
        if (NS_WARN_IF(!parentRow)) {
          return NS_ERROR_FAILURE;
        }

        // We should delete the row instead,
        //  but first check if its the only row left
        //  so we can delete the entire table
        TableSize tableSize(*this, *table, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }

        if (tableSize.mRowCount == 1) {
          return DeleteTable2(table, selection);
        }

        // We need to call DeleteTableRow to handle cells with rowspan
        rv = DeleteTableRow(1);
        NS_ENSURE_SUCCESS(rv, rv);
      } else {
        // More than 1 cell in the row

        // The setCaret object will call AutoSelectionSetterAfterTableEdit in its
        // destructor
        AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                                   startColIndex, ePreviousColumn,
                                                   false);
        AutoTransactionsConserveSelection dontChangeSelection(*this);
        rv = DeleteNodeWithTransaction(*cell);
        // If we fail, don't try to delete any more cells???
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::DeleteTableCellContents()
{
  RefPtr<Selection> selection;
  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;
  nsresult rv = GetCellContext(getter_AddRefs(selection),
                               getter_AddRefs(table),
                               getter_AddRefs(cell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  // Don't fail if no cell found
  NS_ENSURE_TRUE(cell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);


  AutoPlaceholderBatch beginBatching(this);
  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);
  //Don't let Rules System change the selection
  AutoTransactionsConserveSelection dontChangeSelection(*this);


  RefPtr<Element> firstCell;
  rv = GetFirstSelectedCell(nullptr, getter_AddRefs(firstCell));
  NS_ENSURE_SUCCESS(rv, rv);


  if (firstCell) {
    ErrorResult error;
    CellIndexes firstCellIndexes(*firstCell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    cell = firstCell;
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;
  }

  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             startColIndex, ePreviousColumn,
                                             false);

  while (cell) {
    DeleteCellContents(cell);
    if (firstCell) {
      // We doing a selected cells, so do all of them
      rv = GetNextSelectedCell(nullptr, getter_AddRefs(cell));
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      cell = nullptr;
    }
  }
  return NS_OK;
}

nsresult
HTMLEditor::DeleteCellContents(Element* aCell)
{
  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);

  while (nsCOMPtr<nsINode> child = aCell->GetLastChild()) {
    nsresult rv = DeleteNodeWithTransaction(*child);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::DeleteTableColumn(int32_t aNumber)
{
  RefPtr<Selection> selection;
  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;
  nsresult rv = GetCellContext(getter_AddRefs(selection),
                               getter_AddRefs(table),
                               getter_AddRefs(cell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  // Don't fail if no cell found
  NS_ENSURE_TRUE(table && cell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  AutoPlaceholderBatch beginBatching(this);

  // Shortcut the case of deleting all columns in table
  if (!startColIndex && aNumber >= tableSize.mColumnCount) {
    return DeleteTable2(table, selection);
  }

  // Check for counts too high
  aNumber = std::min(aNumber, (tableSize.mColumnCount - startColIndex));

  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);

  // Test if deletion is controlled by selected cells
  RefPtr<Element> firstCell;
  rv = GetFirstSelectedCell(nullptr, getter_AddRefs(firstCell));
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t rangeCount = selection->RangeCount();

  if (firstCell && rangeCount > 1) {
    CellIndexes firstCellIndexes(*firstCell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;
  }
  //We control selection resetting after the insert...
  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             startColIndex, ePreviousRow,
                                             false);

  if (firstCell && rangeCount > 1) {
    // Use selected cells to determine what rows to delete
    cell = firstCell;

    while (cell) {
      if (cell != firstCell) {
        CellIndexes cellIndexes(*cell, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
        startRowIndex = cellIndexes.mRow;
        startColIndex = cellIndexes.mColumn;
      }
      // Find the next cell in a different column
      // to continue after we delete this column
      int32_t nextCol = startColIndex;
      while (nextCol == startColIndex) {
        rv = GetNextSelectedCell(nullptr, getter_AddRefs(cell));
        NS_ENSURE_SUCCESS(rv, rv);
        if (!cell) {
          break;
        }
        CellIndexes cellIndexes(*cell, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
        startRowIndex = cellIndexes.mRow;
        nextCol = cellIndexes.mColumn;
      }
      rv = DeleteColumn(table, startColIndex);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    for (int32_t i = 0; i < aNumber; i++) {
      rv = DeleteColumn(table, startColIndex);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  return NS_OK;
}

nsresult
HTMLEditor::DeleteColumn(Element* aTable,
                         int32_t aColIndex)
{
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  int32_t rowIndex = 0;

  ErrorResult error;
  do {
    nsresult rv =
      GetCellDataAt(aTable, rowIndex, aColIndex, getter_AddRefs(cell),
                    &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                    &actualRowSpan, &actualColSpan, &isSelected);
    NS_ENSURE_SUCCESS(rv, rv);

    if (cell) {
      // Find cells that don't start in column we are deleting
      if (startColIndex < aColIndex || colSpan > 1 || !colSpan) {
        // We have a cell spanning this location
        // Decrease its colspan to keep table rectangular,
        // but if colSpan=0, it will adjust automatically
        if (colSpan > 0) {
          NS_ASSERTION((colSpan > 1),"Bad COLSPAN in DeleteTableColumn");
          SetColSpan(cell, colSpan-1);
        }
        if (startColIndex == aColIndex) {
          // Cell is in column to be deleted, but must have colspan > 1,
          // so delete contents of cell instead of cell itself
          // (We must have reset colspan above)
          DeleteCellContents(cell);
        }
        // To next cell in column
        rowIndex += actualRowSpan;
      } else {
        // Delete the cell
        if (GetNumberOfCellsInRow(aTable, rowIndex) == 1) {
          // Only 1 cell in row - delete the row
          Element* parentRow =
            GetElementOrParentByTagNameInternal(*nsGkAtoms::tr, *cell);
          if (NS_WARN_IF(!parentRow)) {
            return NS_ERROR_FAILURE;
          }

          //  But first check if its the only row left
          //  so we can delete the entire table
          //  (This should never happen but it's the safe thing to do)
          TableSize tableSize(*this, *aTable, error);
          if (NS_WARN_IF(error.Failed())) {
            return error.StealNSResult();
          }

          if (tableSize.mRowCount == 1) {
            RefPtr<Selection> selection = GetSelection();
            NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);
            return DeleteTable2(aTable, selection);
          }

          // Delete the row by placing caret in cell we were to delete
          // We need to call DeleteTableRow to handle cells with rowspan
          rv = DeleteRow(aTable, startRowIndex);
          NS_ENSURE_SUCCESS(rv, rv);

          // Note that we don't incremenet rowIndex
          // since a row was deleted and "next"
          // row now has current rowIndex
        } else {
          // A more "normal" deletion
          rv = DeleteNodeWithTransaction(*cell);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return rv;
          }

          //Skip over any rows spanned by this cell
          rowIndex += actualRowSpan;
        }
      }
    }
  } while (cell);

  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::DeleteTableRow(int32_t aNumber)
{
  RefPtr<Selection> selection;
  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex;
  nsresult rv =  GetCellContext(getter_AddRefs(selection),
                                getter_AddRefs(table),
                                getter_AddRefs(cell),
                                nullptr, nullptr,
                                &startRowIndex, &startColIndex);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (NS_WARN_IF(!table) || NS_WARN_IF(!cell)) {
    // Don't fail if no cell found.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Shortcut the case of deleting all rows in table
  if (!startRowIndex && aNumber >= tableSize.mRowCount) {
    return DeleteTable2(table, selection);
  }

  AutoPlaceholderBatch beginBatching(this);
  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);

  RefPtr<Element> firstCell;
  rv = GetFirstSelectedCell(nullptr, getter_AddRefs(firstCell));
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t rangeCount = selection->RangeCount();
  if (firstCell && rangeCount > 1) {
    // Fetch indexes again - may be different for selected cells
    CellIndexes firstCellIndexes(*firstCell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    startRowIndex = firstCellIndexes.mRow;
    startColIndex = firstCellIndexes.mColumn;
  }

  //We control selection resetting after the insert...
  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             startColIndex, ePreviousRow,
                                             false);
  // Don't change selection during deletions
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  if (firstCell && rangeCount > 1) {
    // Use selected cells to determine what rows to delete
    cell = firstCell;

    while (cell) {
      if (cell != firstCell) {
        CellIndexes cellIndexes(*cell, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
        startRowIndex = cellIndexes.mRow;
        startColIndex = cellIndexes.mColumn;
      }
      // Find the next cell in a different row
      // to continue after we delete this row
      int32_t nextRow = startRowIndex;
      while (nextRow == startRowIndex) {
        rv = GetNextSelectedCell(nullptr, getter_AddRefs(cell));
        NS_ENSURE_SUCCESS(rv, rv);
        if (!cell) {
          break;
        }
        CellIndexes cellIndexes(*cell, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
        nextRow = cellIndexes.mRow;
        startColIndex = cellIndexes.mColumn;
      }
      // Delete entire row
      rv = DeleteRow(table, startRowIndex);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    // Check for counts too high
    aNumber = std::min(aNumber, (tableSize.mRowCount - startRowIndex));
    for (int32_t i = 0; i < aNumber; i++) {
      rv = DeleteRow(table, startRowIndex);
      // If failed in current row, try the next
      if (NS_FAILED(rv)) {
        startRowIndex++;
      }

      // Check if there's a cell in the "next" row
      cell = GetTableCellElementAt(*table, startRowIndex, startColIndex);
      if (!cell) {
        return NS_OK;
      }
    }
  }
  return NS_OK;
}

// Helper that doesn't batch or change the selection
nsresult
HTMLEditor::DeleteRow(Element* aTable,
                      int32_t aRowIndex)
{
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<Element> cell;
  RefPtr<Element> cellInDeleteRow;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  int32_t colIndex = 0;

  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(*
                                      this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);

  // The list of cells we will change rowspan in
  //  and the new rowspan values for each
  nsTArray<RefPtr<Element> > spanCellList;
  nsTArray<int32_t> newSpanList;

  ErrorResult error;
  TableSize tableSize(*this, *aTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Scan through cells in row to do rowspan adjustments
  // Note that after we delete row, startRowIndex will point to the
  //   cells in the next row to be deleted
  do {
    if (aRowIndex >= tableSize.mRowCount ||
        colIndex >= tableSize.mColumnCount) {
      break;
    }

    nsresult rv =
      GetCellDataAt(aTable, aRowIndex, colIndex, getter_AddRefs(cell),
                    &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                    &actualRowSpan, &actualColSpan, &isSelected);
    // We don't fail if we don't find a cell, so this must be real bad
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Compensate for cells that don't start or extend below the row we are deleting
    if (cell) {
      if (startRowIndex < aRowIndex) {
        // Cell starts in row above us
        // Decrease its rowspan to keep table rectangular
        //  but we don't need to do this if rowspan=0,
        //  since it will automatically adjust
        if (rowSpan > 0) {
          // Build list of cells to change rowspan
          // We can't do it now since it upsets cell map,
          //  so we will do it after deleting the row
          spanCellList.AppendElement(cell);
          newSpanList.AppendElement(std::max((aRowIndex - startRowIndex), actualRowSpan-1));
        }
      } else {
        if (rowSpan > 1) {
          // Cell spans below row to delete, so we must insert new cells to
          // keep rows below.  Note that we test "rowSpan" so we don't do this
          // if rowSpan = 0 (automatic readjustment).
          int32_t aboveRowToInsertNewCellInto = aRowIndex - startRowIndex + 1;
          int32_t numOfRawSpanRemainingBelow = actualRowSpan - 1;
          rv = SplitCellIntoRows(aTable, startRowIndex, startColIndex,
                                 aboveRowToInsertNewCellInto,
                                 numOfRawSpanRemainingBelow, nullptr);
          NS_ENSURE_SUCCESS(rv, rv);
        }
        if (!cellInDeleteRow) {
          cellInDeleteRow = cell; // Reference cell to find row to delete
        }
      }
      // Skip over other columns spanned by this cell
      colIndex += actualColSpan;
    }
  } while (cell);

  // Things are messed up if we didn't find a cell in the row!
  if (NS_WARN_IF(!cellInDeleteRow)) {
    return NS_ERROR_FAILURE;
  }

  // Delete the entire row
  RefPtr<Element> parentRow =
    GetElementOrParentByTagNameInternal(*nsGkAtoms::tr, *cellInDeleteRow);
  if (parentRow) {
    nsresult rv = DeleteNodeWithTransaction(*parentRow);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  // Now we can set new rowspans for cells stored above
  for (uint32_t i = 0, n = spanCellList.Length(); i < n; i++) {
    Element* cellPtr = spanCellList[i];
    if (cellPtr) {
      nsresult rv = SetRowSpan(cellPtr, newSpanList[i]);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }
  return NS_OK;
}


NS_IMETHODIMP
HTMLEditor::SelectTable()
{
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_OK; // Don't fail if we didn't find a table.
  }
  RefPtr<Element> table =
    GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::table);
  if (NS_WARN_IF(!table)) {
    return NS_OK; // Don't fail if we didn't find a table.
  }

  nsresult rv = ClearSelection();
  if (NS_FAILED(rv)) {
    return rv;
  }
  return AppendNodeToSelectionAsRange(table);
}

NS_IMETHODIMP
HTMLEditor::SelectTableCell()
{
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  RefPtr<Element> cell =
    GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::td);
  if (NS_WARN_IF(!cell)) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  nsresult rv = ClearSelection();
  if (NS_FAILED(rv)) {
    return rv;
  }
  return AppendNodeToSelectionAsRange(cell);
}

NS_IMETHODIMP
HTMLEditor::SelectBlockOfCells(Element* aStartCell,
                               Element* aEndCell)
{
  if (NS_WARN_IF(!aStartCell) || NS_WARN_IF(!aEndCell)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Element> table =
    GetElementOrParentByTagNameInternal(*nsGkAtoms::table, *aStartCell);
  if (NS_WARN_IF(!table)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Element> endTable =
    GetElementOrParentByTagNameInternal(*nsGkAtoms::table, *aEndCell);
  if (NS_WARN_IF(!endTable)) {
    return NS_ERROR_FAILURE;
  }

  // We can only select a block if within the same table,
  //  so do nothing if not within one table
  if (table != endTable) {
    return NS_OK;
  }

  ErrorResult error;
  CellIndexes startCellIndexes(*aStartCell, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  CellIndexes endCellIndexes(*aEndCell, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Suppress nsISelectionListener notification
  //  until all selection changes are finished
  SelectionBatcher selectionBatcher(selection);

  // Examine all cell nodes in current selection and
  //  remove those outside the new block cell region
  int32_t minColumn =
    std::min(startCellIndexes.mColumn, endCellIndexes.mColumn);
  int32_t minRow =
    std::min(startCellIndexes.mRow, endCellIndexes.mRow);
  int32_t maxColumn =
    std::max(startCellIndexes.mColumn, endCellIndexes.mColumn);
  int32_t maxRow =
    std::max(startCellIndexes.mRow, endCellIndexes.mRow);

  RefPtr<Element> cell;
  int32_t currentRowIndex, currentColIndex;
  RefPtr<nsRange> range;
  nsresult rv =
    GetFirstSelectedCell(getter_AddRefs(range), getter_AddRefs(cell));
  NS_ENSURE_SUCCESS(rv, rv);
  if (rv == NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND) {
    return NS_OK;
  }

  while (cell) {
    CellIndexes currentCellIndexes(*cell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    if (currentCellIndexes.mRow < maxRow ||
        currentCellIndexes.mRow > maxRow ||
        currentCellIndexes.mColumn < maxColumn ||
        currentCellIndexes.mColumn > maxColumn) {
      selection->RemoveRange(*range, IgnoreErrors());
      // Since we've removed the range, decrement pointer to next range
      mSelectedCellIndex--;
    }
    rv = GetNextSelectedCell(getter_AddRefs(range), getter_AddRefs(cell));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  int32_t rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  for (int32_t row = minRow; row <= maxRow; row++) {
    for (int32_t col = minColumn; col <= maxColumn;
        col += std::max(actualColSpan, 1)) {
      rv = GetCellDataAt(table, row, col, getter_AddRefs(cell),
                         &currentRowIndex, &currentColIndex,
                         &rowSpan, &colSpan,
                         &actualRowSpan, &actualColSpan, &isSelected);
      if (NS_FAILED(rv)) {
        break;
      }
      // Skip cells that already selected or are spanned from previous locations
      if (!isSelected && cell &&
          row == currentRowIndex && col == currentColIndex) {
        rv = AppendNodeToSelectionAsRange(cell);
        if (NS_FAILED(rv)) {
          break;
        }
      }
    }
  }
  // NS_OK, otherwise, the last failure of GetCellDataAt() or
  // AppendNodeToSelectionAsRange().
  return rv;
}

NS_IMETHODIMP
HTMLEditor::SelectAllTableCells()
{
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  RefPtr<Element> cell =
    GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::td);
  if (NS_WARN_IF(!cell)) {
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> startCell = cell;

  // Get parent table
  RefPtr<Element> table =
    GetElementOrParentByTagNameInternal(*nsGkAtoms::table, *cell);
  if (NS_WARN_IF(!table)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Suppress nsISelectionListener notification
  //  until all selection changes are finished
  SelectionBatcher selectionBatcher(selection);

  // It is now safe to clear the selection
  // BE SURE TO RESET IT BEFORE LEAVING!
  nsresult rv = ClearSelection();

  // Select all cells in the same column as current cell
  bool cellSelected = false;
  int32_t rowSpan, colSpan, actualRowSpan, actualColSpan, currentRowIndex, currentColIndex;
  bool    isSelected;
  for (int32_t row = 0; row < tableSize.mRowCount; row++) {
    for (int32_t col = 0;
         col < tableSize.mColumnCount;
         col += std::max(actualColSpan, 1)) {
      rv = GetCellDataAt(table, row, col, getter_AddRefs(cell),
                         &currentRowIndex, &currentColIndex,
                         &rowSpan, &colSpan,
                         &actualRowSpan, &actualColSpan, &isSelected);
      if (NS_FAILED(rv)) {
        break;
      }
      // Skip cells that are spanned from previous rows or columns
      if (cell && row == currentRowIndex && col == currentColIndex) {
        rv =  AppendNodeToSelectionAsRange(cell);
        if (NS_FAILED(rv)) {
          break;
        }
        cellSelected = true;
      }
    }
  }
  // Safety code to select starting cell if nothing else was selected
  if (!cellSelected) {
    return AppendNodeToSelectionAsRange(startCell);
  }
  // NS_OK, otherwise, the error of ClearSelection() when there is no column or
  // the last failure of GetCellDataAt() or AppendNodeToSelectionAsRange().
  return rv;
}

NS_IMETHODIMP
HTMLEditor::SelectTableRow()
{
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  RefPtr<Element> cell =
    GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::td);
  if (NS_WARN_IF(!cell)) {
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> startCell = cell;

  // Get table and location of cell:
  RefPtr<Element> table;
  int32_t startRowIndex, startColIndex;

  nsresult rv = GetCellContext(nullptr,
                               getter_AddRefs(table),
                               getter_AddRefs(cell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (NS_WARN_IF(!table)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  //Note: At this point, we could get first and last cells in row,
  //  then call SelectBlockOfCells, but that would take just
  //  a little less code, so the following is more efficient

  // Suppress nsISelectionListener notification
  //  until all selection changes are finished
  SelectionBatcher selectionBatcher(selection);

  // It is now safe to clear the selection
  // BE SURE TO RESET IT BEFORE LEAVING!
  rv = ClearSelection();

  // Select all cells in the same row as current cell
  bool cellSelected = false;
  int32_t rowSpan, colSpan, actualRowSpan, actualColSpan, currentRowIndex, currentColIndex;
  bool    isSelected;
  for (int32_t col = 0;
       col < tableSize.mColumnCount;
       col += std::max(actualColSpan, 1)) {
    rv = GetCellDataAt(table, startRowIndex, col, getter_AddRefs(cell),
                       &currentRowIndex, &currentColIndex, &rowSpan, &colSpan,
                       &actualRowSpan, &actualColSpan, &isSelected);
    if (NS_FAILED(rv)) {
      break;
    }
    // Skip cells that are spanned from previous rows or columns
    if (cell && currentRowIndex == startRowIndex && currentColIndex == col) {
      rv = AppendNodeToSelectionAsRange(cell);
      if (NS_FAILED(rv)) {
        break;
      }
      cellSelected = true;
    }
  }
  // Safety code to select starting cell if nothing else was selected
  if (!cellSelected) {
    return AppendNodeToSelectionAsRange(startCell);
  }
  // NS_OK, otherwise, the error of ClearSelection() when there is no column or
  // the last failure of GetCellDataAt() or AppendNodeToSelectionAsRange().
  return rv;
}

NS_IMETHODIMP
HTMLEditor::SelectTableColumn()
{
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  RefPtr<Element> cell =
    GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::td);
  if (NS_WARN_IF(!cell)) {
    // Don't fail if we didn't find a cell.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  RefPtr<Element> startCell = cell;

  // Get location of cell:
  RefPtr<Element> table;
  int32_t startRowIndex, startColIndex;

  nsresult rv = GetCellContext(nullptr,
                               getter_AddRefs(table),
                               getter_AddRefs(cell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (NS_WARN_IF(!table)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Suppress nsISelectionListener notification
  //  until all selection changes are finished
  SelectionBatcher selectionBatcher(selection);

  // It is now safe to clear the selection
  // BE SURE TO RESET IT BEFORE LEAVING!
  rv = ClearSelection();

  // Select all cells in the same column as current cell
  bool cellSelected = false;
  int32_t rowSpan, colSpan, actualRowSpan, actualColSpan, currentRowIndex, currentColIndex;
  bool    isSelected;
  for (int32_t row = 0;
       row < tableSize.mRowCount;
       row += std::max(actualRowSpan, 1)) {
    rv = GetCellDataAt(table, row, startColIndex, getter_AddRefs(cell),
                       &currentRowIndex, &currentColIndex, &rowSpan, &colSpan,
                       &actualRowSpan, &actualColSpan, &isSelected);
    if (NS_FAILED(rv)) {
      break;
    }
    // Skip cells that are spanned from previous rows or columns
    if (cell && currentRowIndex == row && currentColIndex == startColIndex) {
      rv = AppendNodeToSelectionAsRange(cell);
      if (NS_FAILED(rv)) {
        break;
      }
      cellSelected = true;
    }
  }
  // Safety code to select starting cell if nothing else was selected
  if (!cellSelected) {
    return AppendNodeToSelectionAsRange(startCell);
  }
  // NS_OK, otherwise, the error of ClearSelection() when there is no row or
  // the last failure of GetCellDataAt() or AppendNodeToSelectionAsRange().
  return rv;
}

NS_IMETHODIMP
HTMLEditor::SplitTableCell()
{
  RefPtr<Element> table;
  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, actualRowSpan, actualColSpan;
  // Get cell, table, etc. at selection anchor node
  nsresult rv = GetCellContext(nullptr,
                               getter_AddRefs(table),
                               getter_AddRefs(cell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!table || !cell) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  // We need rowspan and colspan data
  rv = GetCellSpansAt(table, startRowIndex, startColIndex,
                      actualRowSpan, actualColSpan);
  NS_ENSURE_SUCCESS(rv, rv);

  // Must have some span to split
  if (actualRowSpan <= 1 && actualColSpan <= 1) {
    return NS_OK;
  }

  AutoPlaceholderBatch beginBatching(this);
  // Prevent auto insertion of BR in new cell until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eInsertNode,
                                      nsIEditor::eNext);

  // We reset selection
  AutoSelectionSetterAfterTableEdit setCaret(*this, table, startRowIndex,
                                             startColIndex, ePreviousColumn,
                                             false);
  //...so suppress Rules System selection munging
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  RefPtr<Element> newCell;
  int32_t rowIndex = startRowIndex;
  int32_t rowSpanBelow, colSpanAfter;

  // Split up cell row-wise first into rowspan=1 above, and the rest below,
  //  whittling away at the cell below until no more extra span
  for (rowSpanBelow = actualRowSpan-1; rowSpanBelow >= 0; rowSpanBelow--) {
    // We really split row-wise only if we had rowspan > 1
    if (rowSpanBelow > 0) {
      rv = SplitCellIntoRows(table, rowIndex, startColIndex, 1, rowSpanBelow,
                             getter_AddRefs(newCell));
      NS_ENSURE_SUCCESS(rv, rv);
      CopyCellBackgroundColor(newCell, cell);
    }
    int32_t colIndex = startColIndex;
    // Now split the cell with rowspan = 1 into cells if it has colSpan > 1
    for (colSpanAfter = actualColSpan-1; colSpanAfter > 0; colSpanAfter--) {
      rv = SplitCellIntoColumns(table, rowIndex, colIndex, 1, colSpanAfter,
                                getter_AddRefs(newCell));
      NS_ENSURE_SUCCESS(rv, rv);
      CopyCellBackgroundColor(newCell, cell);
      colIndex++;
    }
    // Point to the new cell and repeat
    rowIndex++;
  }
  return NS_OK;
}

nsresult
HTMLEditor::CopyCellBackgroundColor(Element* aDestCell,
                                    Element* aSourceCell)
{
  if (NS_WARN_IF(!aDestCell) || NS_WARN_IF(!aSourceCell)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Copy backgournd color to new cell.
  nsAutoString color;
  bool isSet;
  nsresult rv =
    GetAttributeValue(aSourceCell, NS_LITERAL_STRING("bgcolor"),
                      color, &isSet);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!isSet) {
    return NS_OK;
  }
  return SetAttributeWithTransaction(*aDestCell, *nsGkAtoms::bgcolor, color);
}

nsresult
HTMLEditor::SplitCellIntoColumns(Element* aTable,
                                 int32_t aRowIndex,
                                 int32_t aColIndex,
                                 int32_t aColSpanLeft,
                                 int32_t aColSpanRight,
                                 Element** aNewCell)
{
  NS_ENSURE_TRUE(aTable, NS_ERROR_NULL_POINTER);
  if (aNewCell) {
    *aNewCell = nullptr;
  }

  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  nsresult rv =
    GetCellDataAt(aTable, aRowIndex, aColIndex, getter_AddRefs(cell),
                  &startRowIndex, &startColIndex,
                  &rowSpan, &colSpan,
                  &actualRowSpan, &actualColSpan, &isSelected);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(cell, NS_ERROR_NULL_POINTER);

  // We can't split!
  if (actualColSpan <= 1 || (aColSpanLeft + aColSpanRight) > actualColSpan) {
    return NS_OK;
  }

  // Reduce colspan of cell to split
  rv = SetColSpan(cell, aColSpanLeft);
  NS_ENSURE_SUCCESS(rv, rv);

  // Insert new cell after using the remaining span
  //  and always get the new cell so we can copy the background color;
  RefPtr<Element> newCell;
  rv = InsertCell(cell, actualRowSpan, aColSpanRight, true, false,
                  getter_AddRefs(newCell));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!newCell) {
    return NS_OK;
  }
  if (aNewCell) {
    NS_ADDREF(*aNewCell = newCell.get());
  }
  return CopyCellBackgroundColor(newCell, cell);
}

nsresult
HTMLEditor::SplitCellIntoRows(Element* aTable,
                              int32_t aRowIndex,
                              int32_t aColIndex,
                              int32_t aRowSpanAbove,
                              int32_t aRowSpanBelow,
                              Element** aNewCell)
{
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aNewCell) {
    *aNewCell = nullptr;
  }

  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  nsresult rv =
    GetCellDataAt(aTable, aRowIndex, aColIndex, getter_AddRefs(cell),
                  &startRowIndex, &startColIndex,
                  &rowSpan, &colSpan,
                  &actualRowSpan, &actualColSpan, &isSelected);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (NS_WARN_IF(!cell)) {
    return NS_ERROR_FAILURE;
  }

  // We can't split!
  if (actualRowSpan <= 1 || (aRowSpanAbove + aRowSpanBelow) > actualRowSpan) {
    return NS_OK;
  }

  ErrorResult error;
  TableSize tableSize(*this, *aTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  RefPtr<Element> cell2;
  RefPtr<Element> lastCellFound;
  int32_t startRowIndex2, startColIndex2, rowSpan2, colSpan2, actualRowSpan2, actualColSpan2;
  bool    isSelected2;
  int32_t colIndex = 0;
  bool insertAfter = (startColIndex > 0);
  // This is the row we will insert new cell into
  int32_t rowBelowIndex = startRowIndex+aRowSpanAbove;

  // Find a cell to insert before or after
  for (;;) {
    // Search for a cell to insert before
    rv = GetCellDataAt(aTable, rowBelowIndex,
                       colIndex, getter_AddRefs(cell2),
                       &startRowIndex2, &startColIndex2, &rowSpan2, &colSpan2,
                       &actualRowSpan2, &actualColSpan2, &isSelected2);
    // If we fail here, it could be because row has bad rowspan values,
    //   such as all cells having rowspan > 1 (Call FixRowSpan first!)
    if (NS_FAILED(rv) || !cell) {
      return NS_ERROR_FAILURE;
    }

    // Skip over cells spanned from above (like the one we are splitting!)
    if (cell2 && startRowIndex2 == rowBelowIndex) {
      if (!insertAfter) {
        // Inserting before, so stop at first cell in row we want to insert
        // into.
        break;
      }
      // New cell isn't first in row,
      // so stop after we find the cell just before new cell's column
      if (startColIndex2 + actualColSpan2 == startColIndex) {
        break;
      }
      // If cell found is AFTER desired new cell colum,
      //  we have multiple cells with rowspan > 1 that
      //  prevented us from finding a cell to insert after...
      if (startColIndex2 > startColIndex) {
        // ... so instead insert before the cell we found
        insertAfter = false;
        break;
      }
      lastCellFound = cell2;
    }
    // Skip to next available cellmap location
    colIndex += std::max(actualColSpan2, 1);

    // Done when past end of total number of columns
    if (colIndex > tableSize.mColumnCount) {
      break;
    }
  }

  if (!cell2 && lastCellFound) {
    // Edge case where we didn't find a cell to insert after
    //  or before because column(s) before desired column
    //  and all columns after it are spanned from above.
    //  We can insert after the last cell we found
    cell2 = lastCellFound;
    insertAfter = true; // Should always be true, but let's be sure
  }

  // Reduce rowspan of cell to split
  rv = SetRowSpan(cell, aRowSpanAbove);
  NS_ENSURE_SUCCESS(rv, rv);


  // Insert new cell after using the remaining span
  //  and always get the new cell so we can copy the background color;
  RefPtr<Element> newCell;
  rv = InsertCell(cell2, aRowSpanBelow, actualColSpan, insertAfter, false,
                  getter_AddRefs(newCell));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!newCell) {
    return NS_OK;
  }
  if (aNewCell) {
    NS_ADDREF(*aNewCell = newCell.get());
  }
  return CopyCellBackgroundColor(newCell, cell2);
}

NS_IMETHODIMP
HTMLEditor::SwitchTableCellHeaderType(Element* aSourceCell,
                                      Element** aNewCell)
{
  if (NS_WARN_IF(!aSourceCell)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoPlaceholderBatch beginBatching(this);
  // Prevent auto insertion of BR in new cell created by
  // ReplaceContainerAndCloneAttributesWithTransaction().
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eInsertNode,
                                      nsIEditor::eNext);

  // Save current selection to restore when done.
  // This is needed so ReplaceContainerAndCloneAttributesWithTransaction()
  // can monitor selection when replacing nodes.
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  AutoSelectionRestorer selectionRestorer(selection, this);

  // Set to the opposite of current type
  nsAtom* newCellName =
    aSourceCell->IsHTMLElement(nsGkAtoms::td) ? nsGkAtoms::th : nsGkAtoms::td;

  // This creates new node, moves children, copies attributes (true)
  //   and manages the selection!
  RefPtr<Element> newCell =
    ReplaceContainerAndCloneAttributesWithTransaction(*aSourceCell,
                                                      *newCellName);
  if (NS_WARN_IF(!newCell)) {
    return NS_ERROR_FAILURE;
  }

  // Return the new cell
  if (aNewCell) {
    newCell.forget(aNewCell);
  }

  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::JoinTableCells(bool aMergeNonContiguousContents)
{
  RefPtr<Element> table;
  RefPtr<Element> targetCell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;
  RefPtr<Element> cell2;
  int32_t startRowIndex2, startColIndex2, rowSpan2, colSpan2, actualRowSpan2, actualColSpan2;
  bool    isSelected2;

  // Get cell, table, etc. at selection anchor node
  nsresult rv = GetCellContext(nullptr,
                               getter_AddRefs(table),
                               getter_AddRefs(targetCell),
                               nullptr, nullptr,
                               &startRowIndex, &startColIndex);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!table || !targetCell) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  AutoPlaceholderBatch beginBatching(this);
  //Don't let Rules System change the selection
  AutoTransactionsConserveSelection dontChangeSelection(*this);

  // Note: We dont' use AutoSelectionSetterAfterTableEdit here so the selection
  //  is retained after joining. This leaves the target cell selected
  //  as well as the "non-contiguous" cells, so user can see what happened.

  RefPtr<Element> firstCell;
  int32_t firstRowIndex, firstColIndex;
  rv = GetFirstSelectedCellInTable(&firstRowIndex, &firstColIndex,
                                   getter_AddRefs(firstCell));
  NS_ENSURE_SUCCESS(rv, rv);

  bool joinSelectedCells = false;
  if (firstCell) {
    RefPtr<Element> secondCell;
    rv = GetNextSelectedCell(nullptr, getter_AddRefs(secondCell));
    NS_ENSURE_SUCCESS(rv, rv);

    // If only one cell is selected, join with cell to the right
    joinSelectedCells = (secondCell != nullptr);
  }

  if (joinSelectedCells) {
    // We have selected cells: Join just contiguous cells
    //  and just merge contents if not contiguous
    ErrorResult error;
    TableSize tableSize(*this, *table, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }

    // Get spans for cell we will merge into
    int32_t firstRowSpan, firstColSpan;
    rv = GetCellSpansAt(table, firstRowIndex, firstColIndex,
                        firstRowSpan, firstColSpan);
    NS_ENSURE_SUCCESS(rv, rv);

    // This defines the last indexes along the "edges"
    //  of the contiguous block of cells, telling us
    //  that we can join adjacent cells to the block
    // Start with same as the first values,
    //  then expand as we find adjacent selected cells
    int32_t lastRowIndex = firstRowIndex;
    int32_t lastColIndex = firstColIndex;
    int32_t rowIndex, colIndex;

    // First pass: Determine boundaries of contiguous rectangular block
    //  that we will join into one cell,
    //  favoring adjacent cells in the same row
    for (rowIndex = firstRowIndex; rowIndex <= lastRowIndex; rowIndex++) {
      int32_t currentRowCount = tableSize.mRowCount;
      // Be sure each row doesn't have rowspan errors
      rv = FixBadRowSpan(table, rowIndex, tableSize.mRowCount);
      NS_ENSURE_SUCCESS(rv, rv);
      // Adjust rowcount by number of rows we removed
      lastRowIndex -= currentRowCount - tableSize.mRowCount;

      bool cellFoundInRow = false;
      bool lastRowIsSet = false;
      int32_t lastColInRow = 0;
      int32_t firstColInRow = firstColIndex;
      for (colIndex = firstColIndex;
           colIndex < tableSize.mColumnCount;
           colIndex += std::max(actualColSpan2, 1)) {
        rv = GetCellDataAt(table, rowIndex, colIndex, getter_AddRefs(cell2),
                           &startRowIndex2, &startColIndex2,
                           &rowSpan2, &colSpan2,
                           &actualRowSpan2, &actualColSpan2, &isSelected2);
        NS_ENSURE_SUCCESS(rv, rv);

        if (isSelected2) {
          if (!cellFoundInRow) {
            // We've just found the first selected cell in this row
            firstColInRow = colIndex;
          }
          if (rowIndex > firstRowIndex && firstColInRow != firstColIndex) {
            // We're in at least the second row,
            // but left boundary is "ragged" (not the same as 1st row's start)
            //Let's just end block on previous row
            // and keep previous lastColIndex
            //TODO: We could try to find the Maximum firstColInRow
            //      so our block can still extend down more rows?
            lastRowIndex = std::max(0,rowIndex - 1);
            lastRowIsSet = true;
            break;
          }
          // Save max selected column in this row, including extra colspan
          lastColInRow = colIndex + (actualColSpan2-1);
          cellFoundInRow = true;
        } else if (cellFoundInRow) {
          // No cell or not selected, but at least one cell in row was found
          if (rowIndex > (firstRowIndex + 1) && colIndex <= lastColIndex) {
            // Cell is in a column less than current right border in
            //  the third or higher selected row, so stop block at the previous row
            lastRowIndex = std::max(0,rowIndex - 1);
            lastRowIsSet = true;
          }
          // We're done with this row
          break;
        }
      } // End of column loop

      // Done with this row
      if (cellFoundInRow) {
        if (rowIndex == firstRowIndex) {
          // First row always initializes the right boundary
          lastColIndex = lastColInRow;
        }

        // If we didn't determine last row above...
        if (!lastRowIsSet) {
          if (colIndex < lastColIndex) {
            // (don't think we ever get here?)
            // Cell is in a column less than current right boundary,
            //  so stop block at the previous row
            lastRowIndex = std::max(0,rowIndex - 1);
          } else {
            // Go on to examine next row
            lastRowIndex = rowIndex+1;
          }
        }
        // Use the minimum col we found so far for right boundary
        lastColIndex = std::min(lastColIndex, lastColInRow);
      } else {
        // No selected cells in this row -- stop at row above
        //  and leave last column at its previous value
        lastRowIndex = std::max(0,rowIndex - 1);
      }
    }

    // The list of cells we will delete after joining
    nsTArray<RefPtr<Element>> deleteList;

    // 2nd pass: Do the joining and merging
    for (rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
      for (colIndex = 0;
           colIndex < tableSize.mColumnCount;
           colIndex += std::max(actualColSpan2, 1)) {
        rv = GetCellDataAt(table, rowIndex, colIndex, getter_AddRefs(cell2),
                           &startRowIndex2, &startColIndex2,
                           &rowSpan2, &colSpan2,
                           &actualRowSpan2, &actualColSpan2, &isSelected2);
        NS_ENSURE_SUCCESS(rv, rv);

        // If this is 0, we are past last cell in row, so exit the loop
        if (!actualColSpan2) {
          break;
        }

        // Merge only selected cells (skip cell we're merging into, of course)
        if (isSelected2 && cell2 != firstCell) {
          if (rowIndex >= firstRowIndex && rowIndex <= lastRowIndex &&
              colIndex >= firstColIndex && colIndex <= lastColIndex) {
            // We are within the join region
            // Problem: It is very tricky to delete cells as we merge,
            //  since that will upset the cellmap
            //  Instead, build a list of cells to delete and do it later
            NS_ASSERTION(startRowIndex2 == rowIndex, "JoinTableCells: StartRowIndex is in row above");

            if (actualColSpan2 > 1) {
              //Check if cell "hangs" off the boundary because of colspan > 1
              //  Use split methods to chop off excess
              int32_t extraColSpan = (startColIndex2 + actualColSpan2) - (lastColIndex+1);
              if ( extraColSpan > 0) {
                rv = SplitCellIntoColumns(table, startRowIndex2, startColIndex2,
                                          actualColSpan2 - extraColSpan,
                                          extraColSpan, nullptr);
                NS_ENSURE_SUCCESS(rv, rv);
              }
            }

            rv = MergeCells(firstCell, cell2, false);
            NS_ENSURE_SUCCESS(rv, rv);

            // Add cell to list to delete
            deleteList.AppendElement(cell2.get());
          } else if (aMergeNonContiguousContents) {
            // Cell is outside join region -- just merge the contents
            rv = MergeCells(firstCell, cell2, false);
            NS_ENSURE_SUCCESS(rv, rv);
          }
        }
      }
    }

    // All cell contents are merged. Delete the empty cells we accumulated
    // Prevent rules testing until we're done
    AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                        *this, EditSubAction::eDeleteNode,
                                        nsIEditor::eNext);

    for (uint32_t i = 0, n = deleteList.Length(); i < n; i++) {
      RefPtr<Element> nodeToBeRemoved = deleteList[i];
      if (nodeToBeRemoved) {
        rv = DeleteNodeWithTransaction(*nodeToBeRemoved);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
    // Cleanup selection: remove ranges where cells were deleted
    RefPtr<Selection> selection = GetSelection();
    NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

    uint32_t rangeCount = selection->RangeCount();

    RefPtr<nsRange> range;
    for (uint32_t i = 0; i < rangeCount; i++) {
      range = selection->GetRangeAt(i);
      NS_ENSURE_TRUE(range, NS_ERROR_FAILURE);

      RefPtr<Element> deletedCell;
      GetCellFromRange(range, getter_AddRefs(deletedCell));
      if (!deletedCell) {
        selection->RemoveRange(*range, IgnoreErrors());
        rangeCount--;
        i--;
      }
    }

    // Set spans for the cell everthing merged into
    rv = SetRowSpan(firstCell, lastRowIndex-firstRowIndex+1);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = SetColSpan(firstCell, lastColIndex-firstColIndex+1);
    NS_ENSURE_SUCCESS(rv, rv);


    // Fixup disturbances in table layout
    NormalizeTable(table);
  } else {
    // Joining with cell to the right -- get rowspan and colspan data of target cell
    rv = GetCellDataAt(table, startRowIndex, startColIndex,
                       getter_AddRefs(targetCell),
                       &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                       &actualRowSpan, &actualColSpan, &isSelected);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(targetCell, NS_ERROR_NULL_POINTER);

    // Get data for cell to the right
    rv = GetCellDataAt(table, startRowIndex, startColIndex + actualColSpan,
                       getter_AddRefs(cell2),
                       &startRowIndex2, &startColIndex2, &rowSpan2, &colSpan2,
                       &actualRowSpan2, &actualColSpan2, &isSelected2);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!cell2) {
      return NS_OK; // Don't fail if there's no cell
    }

    // sanity check
    NS_ASSERTION((startRowIndex >= startRowIndex2),"JoinCells: startRowIndex < startRowIndex2");

    // Figure out span of merged cell starting from target's starting row
    // to handle case of merged cell starting in a row above
    int32_t spanAboveMergedCell = startRowIndex - startRowIndex2;
    int32_t effectiveRowSpan2 = actualRowSpan2 - spanAboveMergedCell;

    if (effectiveRowSpan2 > actualRowSpan) {
      // Cell to the right spans into row below target
      // Split off portion below target cell's bottom-most row
      rv = SplitCellIntoRows(table, startRowIndex2, startColIndex2,
                             spanAboveMergedCell+actualRowSpan,
                             effectiveRowSpan2-actualRowSpan, nullptr);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Move contents from cell to the right
    // Delete the cell now only if it starts in the same row
    //   and has enough row "height"
    rv = MergeCells(targetCell, cell2,
                    (startRowIndex2 == startRowIndex) &&
                    (effectiveRowSpan2 >= actualRowSpan));
    NS_ENSURE_SUCCESS(rv, rv);

    if (effectiveRowSpan2 < actualRowSpan) {
      // Merged cell is "shorter"
      // (there are cells(s) below it that are row-spanned by target cell)
      // We could try splitting those cells, but that's REAL messy,
      //  so the safest thing to do is NOT really join the cells
      return NS_OK;
    }

    if (spanAboveMergedCell > 0) {
      // Cell we merged started in a row above the target cell
      // Reduce rowspan to give room where target cell will extend its colspan
      rv = SetRowSpan(cell2, spanAboveMergedCell);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Reset target cell's colspan to encompass cell to the right
    rv = SetColSpan(targetCell, actualColSpan+actualColSpan2);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult
HTMLEditor::MergeCells(RefPtr<Element> aTargetCell,
                       RefPtr<Element> aCellToMerge,
                       bool aDeleteCellToMerge)
{
  NS_ENSURE_TRUE(aTargetCell && aCellToMerge, NS_ERROR_NULL_POINTER);

  // Prevent rules testing until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eDeleteNode,
                                      nsIEditor::eNext);

  // Don't need to merge if cell is empty
  if (!IsEmptyCell(aCellToMerge)) {
    // Get index of last child in target cell
    // If we fail or don't have children,
    //  we insert at index 0
    int32_t insertIndex = 0;

    // Start inserting just after last child
    uint32_t len = aTargetCell->GetChildCount();
    if (len == 1 && IsEmptyCell(aTargetCell)) {
      // Delete the empty node
      nsIContent* cellChild = aTargetCell->GetFirstChild();
      if (NS_WARN_IF(!cellChild)) {
        return NS_ERROR_FAILURE;
      }
      nsresult rv = DeleteNodeWithTransaction(*cellChild);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      insertIndex = 0;
    } else {
      insertIndex = (int32_t)len;
    }

    // Move the contents
    while (aCellToMerge->HasChildren()) {
      nsCOMPtr<nsIContent> cellChild = aCellToMerge->GetLastChild();
      if (NS_WARN_IF(!cellChild)) {
        return NS_ERROR_FAILURE;
      }
      nsresult rv = DeleteNodeWithTransaction(*cellChild);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      rv = InsertNodeWithTransaction(*cellChild,
                                     EditorRawDOMPoint(aTargetCell,
                                                       insertIndex));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  if (!aDeleteCellToMerge) {
    return NS_OK;
  }

  // Delete cells whose contents were moved.
  nsresult rv = DeleteNodeWithTransaction(*aCellToMerge);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}


nsresult
HTMLEditor::FixBadRowSpan(Element* aTable,
                          int32_t aRowIndex,
                          int32_t& aNewRowCount)
{
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  ErrorResult error;
  TableSize tableSize(*this, *aTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;

  int32_t minRowSpan = -1;
  int32_t colIndex;

  for (colIndex = 0;
       colIndex < tableSize.mColumnCount;
       colIndex += std::max(actualColSpan, 1)) {
    nsresult rv =
      GetCellDataAt(aTable, aRowIndex, colIndex, getter_AddRefs(cell),
                    &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                    &actualRowSpan, &actualColSpan, &isSelected);
    // NOTE: This is a *real* failure.
    // GetCellDataAt passes if cell is missing from cellmap
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (!cell) {
      break;
    }
    if (rowSpan > 0 &&
        startRowIndex == aRowIndex &&
        (rowSpan < minRowSpan || minRowSpan == -1)) {
      minRowSpan = rowSpan;
    }
    NS_ASSERTION((actualColSpan > 0),"ActualColSpan = 0 in FixBadRowSpan");
  }
  if (minRowSpan > 1) {
    // The amount to reduce everyone's rowspan
    // so at least one cell has rowspan = 1
    int32_t rowsReduced = minRowSpan - 1;
    for (colIndex = 0;
         colIndex < tableSize.mColumnCount;
         colIndex += std::max(actualColSpan, 1)) {
      nsresult rv =
        GetCellDataAt(aTable, aRowIndex, colIndex, getter_AddRefs(cell),
                      &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                      &actualRowSpan, &actualColSpan, &isSelected);
      if (NS_FAILED(rv)) {
        return rv;
      }
      // Fixup rowspans only for cells starting in current row
      if (cell && rowSpan > 0 &&
          startRowIndex == aRowIndex &&
          startColIndex ==  colIndex ) {
        rv = SetRowSpan(cell, rowSpan-rowsReduced);
        if (NS_FAILED(rv)) {
          return rv;
        }
      }
      NS_ASSERTION((actualColSpan > 0),"ActualColSpan = 0 in FixBadRowSpan");
    }
  }
  tableSize.Update(*this, *aTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  aNewRowCount = tableSize.mRowCount;
  return NS_OK;
}

nsresult
HTMLEditor::FixBadColSpan(Element* aTable,
                          int32_t aColIndex,
                          int32_t& aNewColCount)
{
  if (NS_WARN_IF(!aTable)) {
    return NS_ERROR_INVALID_ARG;
  }

  ErrorResult error;
  TableSize tableSize(*this, *aTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;

  int32_t minColSpan = -1;
  int32_t rowIndex;

  for (rowIndex = 0;
       rowIndex < tableSize.mRowCount;
       rowIndex += std::max(actualRowSpan, 1)) {
    nsresult rv =
      GetCellDataAt(aTable, rowIndex, aColIndex, getter_AddRefs(cell),
                    &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                    &actualRowSpan, &actualColSpan, &isSelected);
    // NOTE: This is a *real* failure.
    // GetCellDataAt passes if cell is missing from cellmap
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (!cell) {
      break;
    }
    if (colSpan > 0 &&
        startColIndex == aColIndex &&
        (colSpan < minColSpan || minColSpan == -1)) {
      minColSpan = colSpan;
    }
    NS_ASSERTION((actualRowSpan > 0),"ActualRowSpan = 0 in FixBadColSpan");
  }
  if (minColSpan > 1) {
    // The amount to reduce everyone's colspan
    // so at least one cell has colspan = 1
    int32_t colsReduced = minColSpan - 1;
    for (rowIndex = 0;
         rowIndex < tableSize.mRowCount;
         rowIndex += std::max(actualRowSpan, 1)) {
      nsresult rv =
        GetCellDataAt(aTable, rowIndex, aColIndex, getter_AddRefs(cell),
                      &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                      &actualRowSpan, &actualColSpan, &isSelected);
      if (NS_FAILED(rv)) {
        return rv;
      }
      // Fixup colspans only for cells starting in current column
      if (cell && colSpan > 0 &&
          startColIndex == aColIndex &&
          startRowIndex ==  rowIndex) {
        rv = SetColSpan(cell, colSpan-colsReduced);
        if (NS_FAILED(rv)) {
          return rv;
        }
      }
      NS_ASSERTION((actualRowSpan > 0),"ActualRowSpan = 0 in FixBadColSpan");
    }
  }
  tableSize.Update(*this, *aTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  aNewColCount = tableSize.mColumnCount;
  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::NormalizeTable(Element* aTable)
{
  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Element> table =
    aTable ?
      GetElementOrParentByTagNameInternal(*nsGkAtoms::table, *aTable) :
      GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::table);
  if (NS_WARN_IF(!table)) {
    // Don't fail if we didn't find a table.
    return NS_OK;
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Save current selection
  AutoSelectionRestorer selectionRestorer(selection, this);

  AutoPlaceholderBatch beginBatching(this);
  // Prevent auto insertion of BR in new cell until we're done
  AutoTopLevelEditSubActionNotifier maybeTopLevelEditSubAction(
                                      *this, EditSubAction::eInsertNode,
                                      nsIEditor::eNext);

  RefPtr<Element> cell;
  int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;

  // Scan all cells in each row to detect bad rowspan values
  for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
    nsresult rv = FixBadRowSpan(table, rowIndex, tableSize.mRowCount);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  // and same for colspans
  for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount; colIndex++) {
    nsresult rv = FixBadColSpan(table, colIndex, tableSize.mColumnCount);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  // Fill in missing cellmap locations with empty cells
  for (int32_t rowIndex = 0; rowIndex < tableSize.mRowCount; rowIndex++) {
    RefPtr<Element> previousCellInRow;
    for (int32_t colIndex = 0; colIndex < tableSize.mColumnCount; colIndex++) {
      nsresult rv =
        GetCellDataAt(table, rowIndex, colIndex, getter_AddRefs(cell),
                      &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                      &actualRowSpan, &actualColSpan, &isSelected);
      // NOTE: This is a *real* failure.
      // GetCellDataAt passes if cell is missing from cellmap
      if (NS_FAILED(rv)) {
        return rv;
      }
      if (!cell) {
        //We are missing a cell at a cellmap location
#ifdef DEBUG
        printf("NormalizeTable found missing cell at row=%d, col=%d\n",
               rowIndex, colIndex);
#endif
        // Add a cell after the previous Cell in the current row
        if (!previousCellInRow) {
          // We don't have any cells in this row -- We are really messed up!
#ifdef DEBUG
          printf("NormalizeTable found no cells in row=%d, col=%d\n",
                 rowIndex, colIndex);
#endif
          return NS_ERROR_FAILURE;
        }

        // Insert a new cell after (true), and return the new cell to us
        rv = InsertCell(previousCellInRow, 1, 1, true, false,
                        getter_AddRefs(cell));
        NS_ENSURE_SUCCESS(rv, rv);

        // Set this so we use returned new "cell" to set previousCellInRow below
        if (cell) {
          startRowIndex = rowIndex;
        }
      }
      // Save the last cell found in the same row we are scanning
      if (startRowIndex == rowIndex) {
        previousCellInRow = cell;
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::GetCellIndexes(Element* aCellElement,
                           int32_t* aRowIndex,
                           int32_t* aColumnIndex)
{
  if (NS_WARN_IF(!aRowIndex) || NS_WARN_IF(!aColumnIndex)) {
    return NS_ERROR_INVALID_ARG;
  }
  *aRowIndex = 0;
  *aColumnIndex = 0;

  if (!aCellElement) {
    // Use cell element which contains anchor of Selection when aCellElement is
    // nullptr.
    RefPtr<Selection> selection = GetSelection();
    if (NS_WARN_IF(!selection)) {
      return NS_ERROR_FAILURE;
    }
    ErrorResult error;
    CellIndexes cellIndexes(*this, *selection, error);
    if (error.Failed()) {
      return error.StealNSResult();
    }
    *aRowIndex = cellIndexes.mRow;
    *aColumnIndex = cellIndexes.mColumn;
    return NS_OK;
  }

  ErrorResult error;
  CellIndexes cellIndexes(*aCellElement, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  *aRowIndex = cellIndexes.mRow;
  *aColumnIndex = cellIndexes.mColumn;
  return NS_OK;
}

void
HTMLEditor::CellIndexes::Update(HTMLEditor& aHTMLEditor,
                                Selection& aSelection,
                                ErrorResult& aRv)
{
  MOZ_ASSERT(!aRv.Failed());

  // Guarantee the life time of the cell element since Init() will access
  // layout methods.
  RefPtr<Element> cellElement =
    aHTMLEditor.GetElementOrParentByTagNameAtSelection(aSelection,
                                                       *nsGkAtoms::td);
  if (!cellElement) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  Update(*cellElement, aRv);
}

void
HTMLEditor::CellIndexes::Update(Element& aCellElement,
                                ErrorResult& aRv)
{
  MOZ_ASSERT(!aRv.Failed());

  // XXX If the table cell is created immediately before this call, e.g.,
  //     using innerHTML, frames have not been created yet.  In such case,
  //     shouldn't we flush pending layout?
  nsIFrame* frameOfCell = aCellElement.GetPrimaryFrame();
  if (NS_WARN_IF(!frameOfCell)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  nsITableCellLayout* tableCellLayout = do_QueryFrame(frameOfCell);
  if (!tableCellLayout) {
    aRv.Throw(NS_ERROR_FAILURE); // not a cell element.
    return;
  }

  aRv = tableCellLayout->GetCellIndexes(mRow, mColumn);
  NS_WARNING_ASSERTION(!aRv.Failed(), "Failed to get cell indexes");
}

// static
nsTableWrapperFrame*
HTMLEditor::GetTableFrame(Element* aTableElement)
{
  if (NS_WARN_IF(!aTableElement)) {
    return nullptr;
  }
  return do_QueryFrame(aTableElement->GetPrimaryFrame());
}

//Return actual number of cells (a cell with colspan > 1 counts as just 1)
int32_t
HTMLEditor::GetNumberOfCellsInRow(Element* aTable,
                                  int32_t rowIndex)
{
  int32_t cellCount = 0;
  RefPtr<Element> cell;
  int32_t colIndex = 0;
  do {
    int32_t startRowIndex, startColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
    bool    isSelected;
    nsresult rv =
      GetCellDataAt(aTable, rowIndex, colIndex, getter_AddRefs(cell),
                    &startRowIndex, &startColIndex, &rowSpan, &colSpan,
                    &actualRowSpan, &actualColSpan, &isSelected);
    NS_ENSURE_SUCCESS(rv, 0);
    if (cell) {
      // Only count cells that start in row we are working with
      if (startRowIndex == rowIndex) {
        cellCount++;
      }
      //Next possible location for a cell
      colIndex += actualColSpan;
    } else {
      colIndex++;
    }
  } while (cell);

  return cellCount;
}

NS_IMETHODIMP
HTMLEditor::GetTableSize(Element* aTableOrElementInTable,
                         int32_t* aRowCount,
                         int32_t* aColumnCount)
{
  if (NS_WARN_IF(!aRowCount) || NS_WARN_IF(!aColumnCount)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aRowCount = 0;
  *aColumnCount = 0;

  Element* tableOrElementInTable = aTableOrElementInTable;
  if (!tableOrElementInTable) {
    RefPtr<Selection> selection = GetSelection();
    if (NS_WARN_IF(!selection)) {
      return NS_ERROR_FAILURE;
    }
    tableOrElementInTable =
      GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::table);
    if (NS_WARN_IF(!tableOrElementInTable)) {
      return NS_ERROR_FAILURE;
    }
  }

  ErrorResult error;
  TableSize tableSize(*this, *tableOrElementInTable, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  *aRowCount = tableSize.mRowCount;
  *aColumnCount = tableSize.mColumnCount;
  return NS_OK;
}

void
HTMLEditor::TableSize::Update(HTMLEditor& aHTMLEditor,
                              Element& aTableOrElementInTable,
                              ErrorResult& aRv)
{
  MOZ_ASSERT(!aRv.Failed());

  // Currently, nsTableWrapperFrame::GetRowCount() and
  // nsTableWrapperFrame::GetColCount() are safe to use without grabbing
  // <table> element.  However, editor developers may not watch layout API
  // changes.  So, for keeping us safer, we should use RefPtr here.
  RefPtr<Element> tableElement =
    aHTMLEditor.GetElementOrParentByTagNameInternal(*nsGkAtoms::table,
                                                    aTableOrElementInTable);
  if (NS_WARN_IF(!tableElement)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  nsTableWrapperFrame* tableFrame =
    do_QueryFrame(tableElement->GetPrimaryFrame());
  if (NS_WARN_IF(!tableFrame)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  mRowCount = tableFrame->GetRowCount();
  mColumnCount = tableFrame->GetColCount();
}

NS_IMETHODIMP
HTMLEditor::GetCellDataAt(Element* aTable,
                          int32_t aRowIndex,
                          int32_t aColIndex,
                          Element** aCell,
                          int32_t* aStartRowIndex,
                          int32_t* aStartColIndex,
                          int32_t* aRowSpan,
                          int32_t* aColSpan,
                          int32_t* aActualRowSpan,
                          int32_t* aActualColSpan,
                          bool* aIsSelected)
{
  NS_ENSURE_ARG_POINTER(aStartRowIndex);
  NS_ENSURE_ARG_POINTER(aStartColIndex);
  NS_ENSURE_ARG_POINTER(aRowSpan);
  NS_ENSURE_ARG_POINTER(aColSpan);
  NS_ENSURE_ARG_POINTER(aActualRowSpan);
  NS_ENSURE_ARG_POINTER(aActualColSpan);
  NS_ENSURE_ARG_POINTER(aIsSelected);
  NS_ENSURE_TRUE(aCell, NS_ERROR_NULL_POINTER);

  *aStartRowIndex = 0;
  *aStartColIndex = 0;
  *aRowSpan = 0;
  *aColSpan = 0;
  *aActualRowSpan = 0;
  *aActualColSpan = 0;
  *aIsSelected = false;

  *aCell = nullptr;

  // needs to live while we use aTable
  // XXX Really? Looks like it's safe to use raw pointer here.
  //     However, layout code change won't be handled by editor developers
  //     so that it must be safe to keep using RefPtr here.
  RefPtr<Element> table;
  if (!aTable) {
    RefPtr<Selection> selection = GetSelection();
    if (NS_WARN_IF(!selection)) {
      return NS_ERROR_FAILURE;
    }
    // Get the selected table or the table enclosing the selection anchor.
    table =
      GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::table);
    if (!table) {
      return NS_ERROR_FAILURE;
    }
    aTable = table;
  }

  nsTableWrapperFrame* tableFrame = HTMLEditor::GetTableFrame(aTable);
  NS_ENSURE_TRUE(tableFrame, NS_ERROR_FAILURE);

  nsTableCellFrame* cellFrame =
    tableFrame->GetCellFrameAt(aRowIndex, aColIndex);
  if (!cellFrame) {
    return NS_ERROR_FAILURE;
  }

  *aIsSelected = cellFrame->IsSelected();
  *aStartRowIndex = cellFrame->RowIndex();
  *aStartColIndex = cellFrame->ColIndex();
  *aRowSpan = cellFrame->GetRowSpan();
  *aColSpan = cellFrame->GetColSpan();
  *aActualRowSpan = tableFrame->GetEffectiveRowSpanAt(aRowIndex, aColIndex);
  *aActualColSpan = tableFrame->GetEffectiveColSpanAt(aRowIndex, aColIndex);
  RefPtr<Element> domCell = cellFrame->GetContent()->AsElement();
  domCell.forget(aCell);

  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::GetCellAt(Element* aTableElement,
                      int32_t aRowIndex,
                      int32_t aColumnIndex,
                      Element** aCellElement)
{
  if (NS_WARN_IF(!aCellElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aCellElement = nullptr;

  Element* tableElement = aTableElement;
  if (!tableElement) {
    RefPtr<Selection> selection = GetSelection();
    if (NS_WARN_IF(!selection)) {
      return NS_ERROR_FAILURE;
    }
    // Get the selected table or the table enclosing the selection anchor.
    tableElement =
      GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::table);
    if (NS_WARN_IF(!tableElement)) {
      return NS_ERROR_FAILURE;
    }
  }

  RefPtr<Element> cellElement =
    GetTableCellElementAt(*tableElement, aRowIndex, aColumnIndex);
  cellElement.forget(aCellElement);
  return NS_OK;
}

Element*
HTMLEditor::GetTableCellElementAt(Element& aTableElement,
                                  int32_t aRowIndex,
                                  int32_t aColumnIndex) const
{
  // Let's grab the <table> element while we're retrieving layout API since
  // editor developers do not watch all layout API changes.  So, it may
  // become unsafe.
  OwningNonNull<Element> tableElement(aTableElement);
  nsTableWrapperFrame* tableFrame = HTMLEditor::GetTableFrame(tableElement);
  if (!tableFrame) {
    return nullptr;
  }
  nsIContent* cell = tableFrame->GetCellAt(aRowIndex, aColumnIndex);
  return Element::FromNodeOrNull(cell);
}

// When all you want are the rowspan and colspan (not exposed in nsITableEditor)
nsresult
HTMLEditor::GetCellSpansAt(Element* aTable,
                           int32_t aRowIndex,
                           int32_t aColIndex,
                           int32_t& aActualRowSpan,
                           int32_t& aActualColSpan)
{
  nsTableWrapperFrame* tableFrame = HTMLEditor::GetTableFrame(aTable);
  if (!tableFrame) {
    return NS_ERROR_FAILURE;
  }
  aActualRowSpan = tableFrame->GetEffectiveRowSpanAt(aRowIndex, aColIndex);
  aActualColSpan = tableFrame->GetEffectiveColSpanAt(aRowIndex, aColIndex);

  return NS_OK;
}

nsresult
HTMLEditor::GetCellContext(Selection** aSelection,
                           Element** aTable,
                           Element** aCell,
                           nsINode** aCellParent,
                           int32_t* aCellOffset,
                           int32_t* aRowIndex,
                           int32_t* aColumnIndex)
{
  // Initialize return pointers
  if (aSelection) {
    *aSelection = nullptr;
  }
  if (aTable) {
    *aTable = nullptr;
  }
  if (aCell) {
    *aCell = nullptr;
  }
  if (aCellParent) {
    *aCellParent = nullptr;
  }
  if (aCellOffset) {
    *aCellOffset = 0;
  }
  if (aRowIndex) {
    *aRowIndex = 0;
  }
  if (aColumnIndex) {
    *aColumnIndex = 0;
  }

  RefPtr<Selection> selection = GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  if (aSelection) {
    *aSelection = selection.get();
    NS_ADDREF(*aSelection);
  }
  RefPtr<Element> table;
  RefPtr<Element> cell;

  // Caller may supply the cell...
  if (aCell && *aCell) {
    cell = *aCell;
  }

  // ...but if not supplied,
  //    get cell if it's the child of selection anchor node,
  //    or get the enclosing by a cell
  if (!cell) {
    // Find a selected or enclosing table element
    RefPtr<Element> cellOrTableElement;
    int32_t selectedCount;
    nsAutoString tagName;
    nsresult rv =
      GetSelectedOrParentTableElement(tagName, &selectedCount,
                                      getter_AddRefs(cellOrTableElement));
    NS_ENSURE_SUCCESS(rv, rv);
    if (tagName.EqualsLiteral("table")) {
      // We have a selected table, not a cell
      if (aTable) {
        cellOrTableElement.forget(aTable);
      }
      return NS_OK;
    }
    if (!tagName.EqualsLiteral("td")) {
      return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
    }

    // We found a cell
    MOZ_ASSERT(cellOrTableElement);
    cell = cellOrTableElement;
  }
  if (aCell) {
    // we don't want to cell.forget() here, because we use it below.
    *aCell = do_AddRef(cell).take();
  }

  // Get containing table
  table = GetElementOrParentByTagNameInternal(*nsGkAtoms::table, *cell);
  if (NS_WARN_IF(!table)) {
    // Cell must be in a table, so fail if not found
    return NS_ERROR_FAILURE;
  }
  if (aTable) {
    table.forget(aTable);
  }

  // Get the rest of the related data only if requested
  if (aRowIndex || aColumnIndex) {
    ErrorResult error;
    CellIndexes cellIndexes(*cell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    if (aRowIndex) {
      *aRowIndex = cellIndexes.mRow;
    }
    if (aColumnIndex) {
      *aColumnIndex = cellIndexes.mColumn;
    }
  }
  if (aCellParent) {
    // Get the immediate parent of the cell
    nsCOMPtr<nsINode> cellParent = cell->GetParentNode();
    // Cell has to have a parent, so fail if not found
    NS_ENSURE_TRUE(cellParent, NS_ERROR_FAILURE);

    if (aCellOffset) {
      *aCellOffset = GetChildOffset(cell, cellParent);
    }

    // Now it's safe to hand over the reference to cellParent, since
    // we don't need it anymore.
    cellParent.forget(aCellParent);
  }

  return NS_OK;
}

nsresult
HTMLEditor::GetCellFromRange(nsRange* aRange,
                             Element** aCell)
{
  // Note: this might return a node that is outside of the range.
  // Use carefully.
  NS_ENSURE_TRUE(aRange && aCell, NS_ERROR_NULL_POINTER);

  *aCell = nullptr;

  nsCOMPtr<nsINode> startContainer = aRange->GetStartContainer();
  if (NS_WARN_IF(!startContainer)) {
    return NS_ERROR_FAILURE;
  }

  uint32_t startOffset = aRange->StartOffset();

  nsCOMPtr<nsINode> childNode = aRange->GetChildAtStartOffset();
  // This means selection is probably at a text node (or end of doc?)
  if (!childNode) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsINode> endContainer = aRange->GetEndContainer();
  if (NS_WARN_IF(!endContainer)) {
    return NS_ERROR_FAILURE;
  }

  // If a cell is deleted, the range is collapse
  //   (startOffset == aRange->EndOffset())
  //   so tell caller the cell wasn't found
  if (startContainer == endContainer &&
      aRange->EndOffset() == startOffset+1 &&
      HTMLEditUtils::IsTableCell(childNode)) {
    // Should we also test if frame is selected? (Use GetCellDataAt())
    // (Let's not for now -- more efficient)
    RefPtr<Element> cellElement = childNode->AsElement();
    cellElement.forget(aCell);
    return NS_OK;
  }
  return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
}

NS_IMETHODIMP
HTMLEditor::GetFirstSelectedCell(nsRange** aRange,
                                 Element** aCell)
{
  NS_ENSURE_TRUE(aCell, NS_ERROR_NULL_POINTER);
  *aCell = nullptr;
  if (aRange) {
    *aRange = nullptr;
  }

  RefPtr<Selection> selection = GetSelection();
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  RefPtr<nsRange> range = selection->GetRangeAt(0);
  NS_ENSURE_TRUE(range, NS_ERROR_FAILURE);

  mSelectedCellIndex = 0;

  nsresult rv = GetCellFromRange(range, aCell);
  // Failure here probably means selection is in a text node,
  //  so there's no selected cell
  if (NS_FAILED(rv)) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  // No cell means range was collapsed (cell was deleted)
  if (!*aCell) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  if (aRange) {
    range.forget(aRange);
  }

  // Setup for next cell
  mSelectedCellIndex = 1;

  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::GetNextSelectedCell(nsRange** aRange,
                                Element** aCell)
{
  NS_ENSURE_TRUE(aCell, NS_ERROR_NULL_POINTER);
  *aCell = nullptr;
  if (aRange) {
    *aRange = nullptr;
  }

  RefPtr<Selection> selection = GetSelection();
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  int32_t rangeCount = selection->RangeCount();

  // Don't even try if index exceeds range count
  if (mSelectedCellIndex >= rangeCount) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  // Scan through ranges to find next valid selected cell
  RefPtr<nsRange> range;
  for (; mSelectedCellIndex < rangeCount; mSelectedCellIndex++) {
    range = selection->GetRangeAt(mSelectedCellIndex);
    NS_ENSURE_TRUE(range, NS_ERROR_FAILURE);

    nsresult rv = GetCellFromRange(range, aCell);
    // Failure here means the range doesn't contain a cell
    NS_ENSURE_SUCCESS(rv, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

    // We found a selected cell
    if (*aCell) {
      break;
    }

    // If we didn't find a cell, continue to next range in selection
  }
  // No cell means all remaining ranges were collapsed (cells were deleted)
  NS_ENSURE_TRUE(*aCell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

  if (aRange) {
    range.forget(aRange);
  }

  // Setup for next cell
  mSelectedCellIndex++;

  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::GetFirstSelectedCellInTable(int32_t* aRowIndex,
                                        int32_t* aColumnIndex,
                                        Element** aCell)
{
  NS_ENSURE_TRUE(aCell, NS_ERROR_NULL_POINTER);
  *aCell = nullptr;
  if (aRowIndex) {
    *aRowIndex = 0;
  }
  if (aColumnIndex) {
    *aColumnIndex = 0;
  }

  RefPtr<Element> cell;
  nsresult rv = GetFirstSelectedCell(nullptr, getter_AddRefs(cell));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(cell, NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND);

  // We don't want to cell.forget() here, because we use "cell" below.
  *aCell = do_AddRef(cell).take();

  if (!aRowIndex && !aColumnIndex) {
    return NS_OK;
  }

  // Also return the row and/or column if requested
  ErrorResult error;
  CellIndexes cellIndexes(*cell, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  if (aRowIndex) {
    *aRowIndex = cellIndexes.mRow;
  }
  if (aColumnIndex) {
    *aColumnIndex = cellIndexes.mColumn;
  }
  return NS_OK;
}

void
HTMLEditor::SetSelectionAfterTableEdit(Element* aTable,
                                       int32_t aRow,
                                       int32_t aCol,
                                       int32_t aDirection,
                                       bool aSelected)
{
  if (NS_WARN_IF(!aTable) || Destroyed()) {
    return;
  }

  RefPtr<Selection> selection = GetSelection();
  if (!selection) {
    return;
  }

  RefPtr<Element> cell;
  bool done = false;
  do {
    cell = GetTableCellElementAt(*aTable, aRow, aCol);
    if (cell) {
      if (aSelected) {
        // Reselect the cell
        DebugOnly<nsresult> rv = SelectContentInternal(*selection, *cell);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
          "Failed to select the cell");
        return;
      }

      // Set the caret to deepest first child
      //   but don't go into nested tables
      // TODO: Should we really be placing the caret at the END
      //  of the cell content?
      CollapseSelectionToDeepestNonTableFirstChild(selection, cell);
      return;
    }

    // Setup index to find another cell in the
    //   direction requested, but move in other direction if already at
    //   beginning of row or column
    switch (aDirection) {
      case ePreviousColumn:
        if (!aCol) {
          if (aRow > 0) {
            aRow--;
          } else {
            done = true;
          }
        } else {
          aCol--;
        }
        break;
      case ePreviousRow:
        if (!aRow) {
          if (aCol > 0) {
            aCol--;
          } else {
            done = true;
          }
        } else {
          aRow--;
        }
        break;
      default:
        done = true;
    }
  } while (!done);

  // We didn't find a cell
  // Set selection to just before the table
  if (aTable->GetParentNode()) {
    EditorRawDOMPoint atTable(aTable);
    if (NS_WARN_IF(!atTable.IsSetAndValid())) {
      return;
    }
    selection->Collapse(atTable);
    return;
  }
  // Last resort: Set selection to start of doc
  // (it's very bad to not have a valid selection!)
  SetSelectionAtDocumentStart(selection);
}

NS_IMETHODIMP
HTMLEditor::GetSelectedOrParentTableElement(nsAString& aTagName,
                                            int32_t* aSelectedCount,
                                            Element** aTableElement)
{
  NS_ENSURE_ARG_POINTER(aTableElement);
  NS_ENSURE_ARG_POINTER(aSelectedCount);
  *aTableElement = nullptr;
  aTagName.Truncate();
  *aSelectedCount = 0;

  RefPtr<Selection> selection = GetSelection();
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  // Try to get the first selected cell
  RefPtr<Element> tableOrCellElement;
  nsresult rv = GetFirstSelectedCell(nullptr,
                                     getter_AddRefs(tableOrCellElement));
  NS_ENSURE_SUCCESS(rv, rv);

  if (tableOrCellElement) {
      // Each cell is in its own selection range,
      //  so count signals multiple-cell selection
      *aSelectedCount = selection->RangeCount();
      aTagName = NS_LITERAL_STRING("td");
  } else {
    nsCOMPtr<nsINode> anchorNode = selection->GetAnchorNode();
    if (NS_WARN_IF(!anchorNode)) {
      return NS_ERROR_FAILURE;
    }

    // Get child of anchor node, if exists
    if (anchorNode->HasChildNodes()) {
      nsINode* selectedNode = selection->GetChildAtAnchorOffset();
      if (selectedNode) {
        if (selectedNode->IsHTMLElement(nsGkAtoms::td)) {
          tableOrCellElement = selectedNode->AsElement();
          aTagName = NS_LITERAL_STRING("td");
          // Each cell is in its own selection range,
          //  so count signals multiple-cell selection
          *aSelectedCount = selection->RangeCount();
        } else if (selectedNode->IsHTMLElement(nsGkAtoms::table)) {
          tableOrCellElement = selectedNode->AsElement();
          aTagName.AssignLiteral("table");
          *aSelectedCount = 1;
        } else if (selectedNode->IsHTMLElement(nsGkAtoms::tr)) {
          tableOrCellElement = selectedNode->AsElement();
          aTagName.AssignLiteral("tr");
          *aSelectedCount = 1;
        }
      }
    }
    if (!tableOrCellElement) {
      // Didn't find a table element -- find a cell parent
      tableOrCellElement =
        GetElementOrParentByTagNameInternal(*nsGkAtoms::td, *anchorNode);
      if (tableOrCellElement) {
        aTagName = NS_LITERAL_STRING("td");
      }
    }
  }
  if (tableOrCellElement) {
    tableOrCellElement.forget(aTableElement);
  }
  return NS_OK;
}

NS_IMETHODIMP
HTMLEditor::GetSelectedCellsType(Element* aElement,
                                 uint32_t* aSelectionType)
{
  NS_ENSURE_ARG_POINTER(aSelectionType);
  *aSelectionType = 0;

  // Be sure we have a table element
  //  (if aElement is null, this uses selection's anchor node)
  RefPtr<Element> table;
  if (aElement) {
    table = GetElementOrParentByTagNameInternal(*nsGkAtoms::table, *aElement);
    if (NS_WARN_IF(!table)) {
      return NS_ERROR_FAILURE;
    }
  } else {
    RefPtr<Selection> selection = GetSelection();
    if (NS_WARN_IF(!selection)) {
      return NS_ERROR_FAILURE;
    }
    table =
      GetElementOrParentByTagNameAtSelection(*selection, *nsGkAtoms::table);
    if (NS_WARN_IF(!table)) {
      return NS_ERROR_FAILURE;
    }
  }

  ErrorResult error;
  TableSize tableSize(*this, *table, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  // Traverse all selected cells
  RefPtr<Element> selectedCell;
  nsresult rv = GetFirstSelectedCell(nullptr, getter_AddRefs(selectedCell));
  NS_ENSURE_SUCCESS(rv, rv);
  if (rv == NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND) {
    return NS_OK;
  }

  // We have at least one selected cell, so set return value
  *aSelectionType = static_cast<uint32_t>(TableSelection::Cell);

  // Store indexes of each row/col to avoid duplication of searches
  nsTArray<int32_t> indexArray;

  bool allCellsInRowAreSelected = false;
  bool allCellsInColAreSelected = false;
  while (NS_SUCCEEDED(rv) && selectedCell) {
    CellIndexes selectedCellIndexes(*selectedCell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
    if (!indexArray.Contains(selectedCellIndexes.mColumn)) {
      indexArray.AppendElement(selectedCellIndexes.mColumn);
      allCellsInRowAreSelected =
        AllCellsInRowSelected(table, selectedCellIndexes.mRow,
                              tableSize.mColumnCount);
      // We're done as soon as we fail for any row
      if (!allCellsInRowAreSelected) {
        break;
      }
    }
    rv = GetNextSelectedCell(nullptr, getter_AddRefs(selectedCell));
  }

  if (allCellsInRowAreSelected) {
    *aSelectionType = static_cast<uint32_t>(TableSelection::Row);
    return NS_OK;
  }
  // Test for columns

  // Empty the indexArray
  indexArray.Clear();

  // Start at first cell again
  rv = GetFirstSelectedCell(nullptr, getter_AddRefs(selectedCell));
  while (NS_SUCCEEDED(rv) && selectedCell) {
    CellIndexes selectedCellIndexes(*selectedCell, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }

    if (!indexArray.Contains(selectedCellIndexes.mRow)) {
      indexArray.AppendElement(selectedCellIndexes.mColumn);
      allCellsInColAreSelected =
        AllCellsInColumnSelected(table, selectedCellIndexes.mColumn,
                                 tableSize.mRowCount);
      // We're done as soon as we fail for any column
      if (!allCellsInRowAreSelected) {
        break;
      }
    }
    rv = GetNextSelectedCell(nullptr, getter_AddRefs(selectedCell));
  }
  if (allCellsInColAreSelected) {
    *aSelectionType = static_cast<uint32_t>(TableSelection::Column);
  }

  return NS_OK;
}

bool
HTMLEditor::AllCellsInRowSelected(Element* aTable,
                                  int32_t aRowIndex,
                                  int32_t aNumberOfColumns)
{
  NS_ENSURE_TRUE(aTable, false);

  int32_t curStartRowIndex, curStartColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;

  for (int32_t col = 0; col < aNumberOfColumns;
       col += std::max(actualColSpan, 1)) {
    RefPtr<Element> cell;
    nsresult rv = GetCellDataAt(aTable, aRowIndex, col, getter_AddRefs(cell),
                                &curStartRowIndex, &curStartColIndex,
                                &rowSpan, &colSpan,
                                &actualRowSpan, &actualColSpan, &isSelected);

    NS_ENSURE_SUCCESS(rv, false);
    // If no cell, we may have a "ragged" right edge,
    //   so return TRUE only if we already found a cell in the row
    NS_ENSURE_TRUE(cell, (col > 0) ? true : false);

    // Return as soon as a non-selected cell is found
    NS_ENSURE_TRUE(isSelected, false);

    NS_ASSERTION((actualColSpan > 0),"ActualColSpan = 0 in AllCellsInRowSelected");
  }
  return true;
}

bool
HTMLEditor::AllCellsInColumnSelected(Element* aTable,
                                     int32_t aColIndex,
                                     int32_t aNumberOfRows)
{
  NS_ENSURE_TRUE(aTable, false);

  int32_t curStartRowIndex, curStartColIndex, rowSpan, colSpan, actualRowSpan, actualColSpan;
  bool    isSelected;

  for (int32_t row = 0; row < aNumberOfRows;
       row += std::max(actualRowSpan, 1)) {
    RefPtr<Element> cell;
    nsresult rv = GetCellDataAt(aTable, row, aColIndex, getter_AddRefs(cell),
                                &curStartRowIndex, &curStartColIndex,
                                &rowSpan, &colSpan,
                                &actualRowSpan, &actualColSpan, &isSelected);

    NS_ENSURE_SUCCESS(rv, false);
    // If no cell, we must have a "ragged" right edge on the last column
    //   so return TRUE only if we already found a cell in the row
    NS_ENSURE_TRUE(cell, (row > 0) ? true : false);

    // Return as soon as a non-selected cell is found
    NS_ENSURE_TRUE(isSelected, false);
  }
  return true;
}

bool
HTMLEditor::IsEmptyCell(dom::Element* aCell)
{
  MOZ_ASSERT(aCell);

  // Check if target only contains empty text node or <br>
  nsCOMPtr<nsINode> cellChild = aCell->GetFirstChild();
  if (!cellChild) {
    return false;
  }

  nsCOMPtr<nsINode> nextChild = cellChild->GetNextSibling();
  if (nextChild) {
    return false;
  }

  // We insert a single break into a cell by default
  //   to have some place to locate a cursor -- it is dispensable
  if (cellChild->IsHTMLElement(nsGkAtoms::br)) {
    return true;
  }

  bool isEmpty;
  // Or check if no real content
  nsresult rv = IsEmptyNode(cellChild, &isEmpty, false, false);
  NS_ENSURE_SUCCESS(rv, false);
  return isEmpty;
}

} // namespace mozilla
