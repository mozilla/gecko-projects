/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */
#include "nsSMIMEStub.h"
#include "prlog.h"
#include "prmem.h"
#include "plstr.h"
#include "mimexpcom.h"
#include "mimecth.h"
#include "mimeobj.h"
#include "nsCRT.h"

// String bundles...
#include "nsIStringBundle.h"

#define SMIME_PROPERTIES_URL          "chrome://messenger/locale/smime.properties"
#define SMIME_STR_NOT_SUPPORTED_ID    1000

static NS_DEFINE_CID(kStringBundleServiceCID, NS_STRINGBUNDLESERVICE_CID);
#ifndef XP_MAC
static nsCOMPtr<nsIStringBundle> stringBundle = nsnull;
#endif


//
// This is the next generation string retrieval call 
//
static char *SMimeGetStringByID(PRInt32 aMsgId)
{
  char          *tempString = nsnull;
	nsresult res = NS_OK;

#ifdef XP_MAC
nsCOMPtr<nsIStringBundle> stringBundle = nsnull;
#endif

	if (!stringBundle)
	{
		char*       propertyURL = NULL;

		propertyURL = SMIME_PROPERTIES_URL;

		nsCOMPtr<nsIStringBundleService> sBundleService = 
		         do_GetService(kStringBundleServiceCID, &res); 
		if (NS_SUCCEEDED(res) && (nsnull != sBundleService)) 
		{
			res = sBundleService->CreateBundle(propertyURL, getter_AddRefs(stringBundle));
		}
	}

	if (stringBundle)
	{
		PRUnichar *ptrv = nsnull;
		res = stringBundle->GetStringFromID(aMsgId, &ptrv);

		if (NS_FAILED(res)) 
      return nsCRT::strdup("???");
		else
    {
      nsAutoString v;
      v.Append(ptrv);
	    PR_FREEIF(ptrv);
      tempString = v.ToNewUTF8String();
    }
	}

  if (!tempString)
    return nsCRT::strdup("???");
  else
    return tempString;
}




static int MimeInlineTextSMIMEStub_parse_line (char *, PRInt32, MimeObject *);
static int MimeInlineTextSMIMEStub_parse_eof (MimeObject *, PRBool);
static int MimeInlineTextSMIMEStub_parse_begin (MimeObject *obj);

 /* This is the object definition. Note: we will set the superclass 
    to NULL and manually set this on the class creation */

MimeDefClass(MimeInlineTextSMIMEStub, MimeInlineTextSMIMEStubClass, mimeInlineTextSMIMEStubClass, NULL);  

extern "C" char *
MIME_GetContentType(void)
{
  return SMIME_CONTENT_TYPE;
}


extern "C" MimeObjectClass *
MIME_CreateContentTypeHandlerClass(const char *content_type, 
                                   contentTypeHandlerInitStruct *initStruct)
{
  MimeObjectClass *clazz = (MimeObjectClass *)&mimeInlineTextSMIMEStubClass;
  /* 
   * Must set the superclass by hand.
   */
  if (!COM_GetmimeInlineTextClass())
    return NULL;

  clazz->superclass = (MimeObjectClass *)COM_GetmimeInlineTextClass();
  initStruct->force_inline_display = PR_TRUE;
  return clazz;
}

static int
MimeInlineTextSMIMEStubClassInitialize(MimeInlineTextSMIMEStubClass *clazz)
{
  MimeObjectClass *oclass = (MimeObjectClass *) clazz;
  PR_ASSERT(!oclass->class_initialized);
  oclass->parse_begin = MimeInlineTextSMIMEStub_parse_begin;
  oclass->parse_line  = MimeInlineTextSMIMEStub_parse_line;
  oclass->parse_eof   = MimeInlineTextSMIMEStub_parse_eof;

  return 0;
}

int
GenerateMessage(char** html) 
{
  nsCString temp;
  temp.Append("<BR><text=\"#000000\" bgcolor=\"#FFFFFF\" link=\"#FF0000\" vlink=\"#800080\" alink=\"#0000FF\">");
  temp.Append("<center><table BORDER=1 ><tr><td><CENTER>");

  char *tString = SMimeGetStringByID(SMIME_STR_NOT_SUPPORTED_ID);
  temp.Append(tString);
  PR_FREEIF(tString);

  temp.Append("</CENTER></td></tr></table></center><BR>");
  *html = temp.ToNewCString();
  return 0;
}

static int
MimeInlineTextSMIMEStub_parse_begin(MimeObject *obj)
{
  MimeInlineTextSMIMEStubClass *clazz;
  int status = ((MimeObjectClass*)COM_GetmimeLeafClass())->parse_begin(obj);
  
  if (status < 0) 
    return status;
  
  if (!obj->output_p) 
    return 0;
  if (!obj->options || !obj->options->write_html_p) 
    return 0;
  
  /* This is a fine place to write out any HTML before the real meat begins. */  

  // Initialize the clazz variable...
  clazz = ((MimeInlineTextSMIMEStubClass *) obj->clazz);
  return 0;
}

static int
MimeInlineTextSMIMEStub_parse_line(char *line, PRInt32 length, MimeObject *obj)
{
 /* 
  * This routine gets fed each line of data, one at a time. We just buffer
  * it all up, to be dealt with all at once at the end. 
  */  
  if (!obj->output_p) 
    return 0;
  if (!obj->options || !obj->options->output_fn) 
    return 0;

  if (!obj->options->write_html_p) 
  {
    return COM_MimeObject_write(obj, line, length, PR_TRUE);
  }
  
  return 0;
}

static int
MimeInlineTextSMIMEStub_parse_eof (MimeObject *obj, PRBool abort_p)
{
  int status = 0;
  char* html = NULL;
  
  if (obj->closed_p) return 0;

  /* Run parent method first, to flush out any buffered data. */
  status = ((MimeObjectClass*)COM_GetmimeInlineTextClass())->parse_eof(obj, abort_p);
  if (status < 0) 
    return status;

  if (  (obj->options) && 
        ((obj->options->format_out == nsMimeOutput::nsMimeMessageQuoting) ||
         (obj->options->format_out == nsMimeOutput::nsMimeMessageBodyQuoting))
     )
    return 0;

  status = GenerateMessage(&html);
  if (status < 0) 
    return status;
  
  status = COM_MimeObject_write(obj, html, PL_strlen(html), PR_TRUE);
  PR_FREEIF(html);
  if (status < 0) 
    return status;
 
  return 0;
}
