/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Daniel Glazman <glazman@netscape.com>
 *
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsEditProperty.h"
#include "nsString.h"
#include "nsStaticAtom.h"

// singleton instance
static nsEditProperty *gInstance;

NS_IMPL_ADDREF(nsEditProperty)

NS_IMPL_RELEASE(nsEditProperty)


#define EDITOR_ATOM(name_, value_) nsIAtom* nsIEditProperty::name_ = 0;
#include "nsEditPropertyAtomList.h"
#undef EDITOR_ATOM

// special
nsString * nsIEditProperty::allProperties;

/* From the HTML 4.0 DTD, 

INLINE:
<!-- %inline; covers inline or "text-level" elements -->
 <!ENTITY % inline "#PCDATA | %fontstyle; | %phrase; | %special; | %formctrl;">
 <!ENTITY % fontstyle "TT | I | B | BIG | SMALL">
 <!ENTITY % phrase "EM | STRONG | DFN | CODE |
                    SAMP | KBD | VAR | CITE | ABBR | ACRONYM" >
 <!ENTITY % special
    "A | IMG | OBJECT | BR | SCRIPT | MAP | Q | SUB | SUP | SPAN | BDO">
 <!ENTITY % formctrl "INPUT | SELECT | TEXTAREA | LABEL | BUTTON">

BLOCK:
<!ENTITY % block
      "P | %heading (h1-h6); | %list (UL | OL); | %preformatted (PRE); | DL | DIV | NOSCRIPT |
       BLOCKQUOTE | FORM | HR | TABLE | FIELDSET | ADDRESS">

But what about BODY, TR, TD, TH, CAPTION, COL, COLGROUP, THEAD, TFOOT, LI, DT, DD, LEGEND, etc.?
 

*/
nsEditProperty::nsEditProperty()
{
  // inline tags
  static const nsStaticAtom property_atoms[] = {
#define EDITOR_ATOM(name_, value_) { value_, &name_ },
#include "nsEditPropertyAtomList.h"
#undef EDITOR_ATOM
  };
  
  NS_RegisterStaticAtoms(property_atoms, NS_ARRAY_LENGTH(property_atoms));
  
  // special
  if ( (nsIEditProperty::allProperties = new nsString) != nsnull )
    nsIEditProperty::allProperties->Assign(NS_LITERAL_STRING("moz_allproperties"));
}

nsEditProperty::~nsEditProperty()
{
  // special
  if (nsIEditProperty::allProperties) {
    delete (nsIEditProperty::allProperties);
    nsIEditProperty::allProperties = nsnull;
  }
  gInstance = nsnull;
}

NS_IMETHODIMP
nsEditProperty::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
  if (nsnull == aInstancePtr) {
    return NS_ERROR_NULL_POINTER;
  }
  if (aIID.Equals(NS_GET_IID(nsISupports))) {
    *aInstancePtr = (void*)(nsISupports*)this;
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIEditProperty))) {
    *aInstancePtr = (void*)(nsIEditProperty*)this;
    NS_ADDREF_THIS();
    return NS_OK;
  }
  return NS_NOINTERFACE;
}

/* Factory for edit property object */
nsresult NS_NewEditProperty(nsIEditProperty **aResult)
{
  if (aResult)
  {
    if (!gInstance)
    {
      gInstance = new nsEditProperty();
      if (!gInstance) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
    *aResult = gInstance;
    NS_ADDREF(*aResult);
    return NS_OK;
  }
  return NS_ERROR_NULL_POINTER;
}
