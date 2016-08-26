/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <string.h>
#include "nsXPCOM.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsISupports.h"
#include "nsICharsetConverterManager.h"
#include "nsIPlatformCharset.h"
#include "nsReadableUtils.h"


static NS_DEFINE_CID(kCharsetConverterManagerCID, NS_ICHARSETCONVERTERMANAGER_CID);
static NS_DEFINE_CID(kPlatformCharsetCID, NS_PLATFORMCHARSET_CID);

/**
 * Test program for the Unicode Converters.
 *
 * Error messages format inside of a test.
 *
 * - silent while all is OK.
 * 
 * - "ERROR at T001.easyConversion.Convert() code=0xfffd.\n"
 * - "ERROR at T001.easyConversion.ConvResLen expected=0x02 result=0x04.\n"
 * 
 * - "Test Passed.\n" for a successful end.
 *
 * @created         01/Dec/1998
 * @author  Catalin Rotaru [CATA]
 */

//----------------------------------------------------------------------
// Global variables and macros

#define GENERAL_BUFFER 20000 // general purpose buffer; for Unicode divide by 2

#define ARRAY_SIZE(_array)                                      \
     (sizeof(_array) / sizeof(_array[0]))

nsICharsetConverterManager * ccMan = nullptr;

/**
 * Test data for Latin1 charset.
 */

char bLatin1_d0[] = {
  "\x00\x0d\x7f\x80\xff"
};

char16_t cLatin1_d0[] = {
  0x0000,0x000d,0x007f,0x20ac,0x00ff
};

int32_t bLatin1_s0 = ARRAY_SIZE(bLatin1_d0)-1;
int32_t cLatin1_s0 = ARRAY_SIZE(cLatin1_d0);

//----------------------------------------------------------------------
// Converter Manager test code

nsresult testCharsetConverterManager()
{
  printf("\n[T001] CharsetConverterManager\n");

  return NS_OK;
}

//----------------------------------------------------------------------
// Helper functions and macros for testing decoders and encoders

#define CREATE_DECODER(_charset)                                \
    nsIUnicodeDecoder * dec;                                    \
    nsAutoString str;str.AssignASCII(_charset);                 \
    nsresult res = ccMan->GetUnicodeDecoder(&str,&dec);         \
    if (NS_FAILED(res)) {                                       \
      printf("ERROR at GetUnicodeDecoder() code=0x%x.\n",res);  \
      return res;                                               \
    }

#define CREATE_ENCODER(_charset)                                \
    nsIUnicodeEncoder * enc;                                    \
    nsAutoString str; str.AssignASCII(_charset);                \
    nsresult res = ccMan->GetUnicodeEncoder(&str,&enc);         \
    if (NS_FAILED(res)) {                                       \
      printf("ERROR at GetUnicodeEncoder() code=0x%x.\n",res);  \
      return res;                                               \
    }

/**
 * Decoder test.
 * 
 * This method will test the conversion only.
 */
nsresult testDecoder(nsIUnicodeDecoder * aDec, 
                     const char * aSrc, int32_t aSrcLength, 
                     const char16_t * aRes, int32_t aResLength,
                     const char * aTestName)
{
  nsresult res;

  // prepare for conversion
  int32_t srcLen = aSrcLength;
  char16_t dest[GENERAL_BUFFER/2];
  int32_t destLen = GENERAL_BUFFER/2;

  // conversion
  res = aDec->Convert(aSrc, &srcLen, dest, &destLen);
  // we want a perfect result here - the test data should be complete!
  if (res != NS_OK) {
    printf("ERROR at %s.easy.Decode() code=0x%x.\n",aTestName,res);
    return NS_ERROR_UNEXPECTED;
  }

  // compare results
  if (aResLength != destLen) {
      printf("ERROR at %s.easy.DecResLen expected=0x%x result=0x%x.\n", 
          aTestName, aResLength, destLen);
      return NS_ERROR_UNEXPECTED;
  }
  for (int32_t i=0; i<aResLength; i++) if (aRes[i] != dest[i]) {
      printf("ERROR at %s.easy.DecResChar[%d] expected=0x%x result=0x%x.\n", 
          aTestName, i, aRes[i], dest[i]);
      return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

/**
 * Encoder test.
 * 
 * This method will test the conversion only.
 */
nsresult testEncoder(nsIUnicodeEncoder * aEnc, 
                     const char16_t * aSrc, int32_t aSrcLength, 
                     const char * aRes, int32_t aResLength,
                     const char * aTestName)
{
  nsresult res;

  // prepare for conversion
  int32_t srcLen = 0;
  char dest[GENERAL_BUFFER];
  int32_t destLen = 0;
  int32_t bcr, bcw;

  // conversion
  bcr = aSrcLength;
  bcw = GENERAL_BUFFER;
  res = aEnc->Convert(aSrc, &bcr, dest, &bcw);
  srcLen += bcr;
  destLen += bcw;
  // we want a perfect result here - the test data should be complete!
  if (res != NS_OK) {
    printf("ERROR at %s.easy.Encode() code=0x%x.\n",aTestName,res);
    return NS_ERROR_UNEXPECTED;
  }

  // finish
  bcw = GENERAL_BUFFER - destLen;
  res = aEnc->Finish(dest + destLen, &bcw);
  destLen += bcw;
  // we want a perfect result here - the test data should be complete!
  if (res != NS_OK) {
    printf("ERROR at %s.easy.Finish() code=0x%x.\n",aTestName,res);
    return NS_ERROR_UNEXPECTED;
  }

  // compare results
  if (aResLength != destLen) {
      printf("ERROR at %s.easy.EncResLen expected=0x%x result=0x%x.\n", 
          aTestName, aResLength, destLen);
      return NS_ERROR_UNEXPECTED;
  }
  for (int32_t i=0; i<aResLength; i++) if (aRes[i] != dest[i]) {
      printf("ERROR at %s.easy.EncResChar[%d] expected=0x%x result=0x%x.\n", 
          aTestName, i, aRes[i], dest[i]);
      return NS_ERROR_UNEXPECTED;
  }
  
  return NS_OK;
}

/**
 * Decoder test.
 * 
 * This method will test a given converter under a given set of data and some 
 * very stressful conditions.
 */
nsresult testStressDecoder(nsIUnicodeDecoder * aDec, 
                           const char * aSrc, int32_t aSrcLength, 
                           const char16_t * aRes, int32_t aResLength,
                           const char * aTestName)
{
  nsresult res;

  // get estimated length
  int32_t estimatedLength;
  res = aDec->GetMaxLength(aSrc, aSrcLength, &estimatedLength);
  if (NS_FAILED(res)) {
    printf("ERROR at %s.stress.Length() code=0x%x.\n",aTestName,res);
    return res;
  }
  bool exactLength = (res == NS_EXACT_LENGTH);

  // prepare for conversion
  int32_t srcLen = 0;
  int32_t srcOff = 0;
  char16_t dest[1024];
  int32_t destLen = 0;
  int32_t destOff = 0;

  // controlled conversion
  for (;srcOff < aSrcLength;) {
    res = aDec->Convert(aSrc + srcOff, &srcLen, dest + destOff, &destLen);
    if (NS_FAILED(res)) {
      printf("ERROR at %s.stress.Convert() code=0x%x.\n",aTestName,res);
      return res;
    }

    srcOff+=srcLen;
    destOff+=destLen;

    // give a little input each time; it'll be consumed if enough output space
    srcLen = 1;
    // give output space only when requested: sadic!
    if (res == NS_PARTIAL_MORE_OUTPUT) {
      destLen = 1;
    } else {
      destLen = 0;
    }
  }

  // we want perfect result here - the test data should be complete!
  if (res != NS_OK) {
    printf("ERROR at %s.stress.postConvert() code=0x%x.\n",aTestName,res);
    return NS_ERROR_UNEXPECTED;
  }

  // compare lengths
  if (exactLength) {
    if (destOff != estimatedLength) {
      printf("ERROR at %s.stress.EstimatedLen expected=0x%x result=0x%x.\n",
          aTestName, estimatedLength, destOff);
      return NS_ERROR_UNEXPECTED;
    }
  } else {
    if (destOff > estimatedLength) {
      printf("ERROR at %s.stress.EstimatedLen expected<=0x%x result=0x%x.\n",
          aTestName, estimatedLength, destOff);
      return NS_ERROR_UNEXPECTED;
    }
  }

  // compare results
  if (aResLength != destOff) {
      printf("ERROR at %s.stress.ConvResLen expected=0x%x result=0x%x.\n", 
          aTestName, aResLength, destOff);
      return NS_ERROR_UNEXPECTED;
  }
  for (int32_t i=0; i<aResLength; i++) if (aRes[i] != dest[i]) {
      printf("ERROR at %s.stress.ConvResChar[%d] expected=0x%x result=0x%x.\n", 
          aTestName, i, aRes[i], dest[i]);
      return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

/**
 * Encoder test.
 * 
 * This method will test a given converter under a given set of data and some 
 * very stressful conditions.
 */
nsresult testStressEncoder(nsIUnicodeEncoder * aEnc, 
                           const char16_t * aSrc, int32_t aSrcLength,
                           const char * aRes, int32_t aResLength, 
                           const char * aTestName)
{
  nsresult res;

  // get estimated length
  int32_t estimatedLength;
  res = aEnc->GetMaxLength(aSrc, aSrcLength, &estimatedLength);
  if (NS_FAILED(res)) {
    printf("ERROR at %s.stress.Length() code=0x%x.\n",aTestName,res);
    return res;
  }
  bool exactLength = (res == NS_OK_UENC_EXACTLENGTH);

  // prepare for conversion
  int32_t srcLen = 0;
  int32_t srcOff = 0;
  char dest[GENERAL_BUFFER];
  int32_t destLen = 0;
  int32_t destOff = 0;

  // controlled conversion
  for (;srcOff < aSrcLength;) {
    res = aEnc->Convert(aSrc + srcOff, &srcLen, dest + destOff, &destLen);
    if (NS_FAILED(res)) {
      printf("ERROR at %s.stress.Convert() code=0x%x.\n",aTestName,res);
      return res;
    }

    srcOff+=srcLen;
    destOff+=destLen;

    // give a little input each time; it'll be consumed if enough output space
    srcLen = 1;
    // give output space only when requested: sadic!
    if (res == NS_OK_UENC_MOREOUTPUT) {
      destLen = 1;
    } else {
      destLen = 0;
    }
  }

  if (res != NS_OK) if (res != NS_OK_UENC_MOREOUTPUT) {
    printf("ERROR at %s.stress.postConvert() code=0x%x.\n",aTestName,res);
    return NS_ERROR_UNEXPECTED;
  } 
  
  for (;;) {
    res = aEnc->Finish(dest + destOff, &destLen);
    if (NS_FAILED(res)) {
      printf("ERROR at %s.stress.Finish() code=0x%x.\n",aTestName,res);
      return res;
    }

    destOff+=destLen;

    // give output space only when requested: sadic!
    if (res == NS_OK_UENC_MOREOUTPUT) {
      destLen = 1;
    } else break;
  }

  // compare lengths
  if (exactLength) {
    if (destOff != estimatedLength) {
      printf("ERROR at %s.stress.EstimatedLen expected=0x%x result=0x%x.\n",
          aTestName, estimatedLength, destOff);
      return NS_ERROR_UNEXPECTED;
    }
  } else {
    if (destOff > estimatedLength) {
      printf("ERROR at %s.stress.EstimatedLen expected<=0x%x result=0x%x.\n",
          aTestName, estimatedLength, destOff);
      return NS_ERROR_UNEXPECTED;
    }
  }

  // compare results
  if (aResLength != destOff) {
      printf("ERROR at %s.stress.ConvResLen expected=0x%x result=0x%x.\n", 
          aTestName, aResLength, destOff);
      return NS_ERROR_UNEXPECTED;
  }
  for (int32_t i=0; i<aResLength; i++) if (aRes[i] != dest[i]) {
      printf("ERROR at %s.stress.ConvResChar[%d] expected=0x%x result=0x%x.\n", 
          aTestName, i, aRes[i], dest[i]);
      return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

/**
 * Reset decoder.
 */
nsresult resetDecoder(nsIUnicodeDecoder * aDec, const char * aTestName)
{
  nsresult res = aDec->Reset();

  if (NS_FAILED(res)) {
    printf("ERROR at %s.dec.Reset() code=0x%x.\n",aTestName,res);
    return res;
  }

  return res;
}

/**
 * Reset encoder.
 */
nsresult resetEncoder(nsIUnicodeEncoder * aEnc, const char * aTestName)
{
  nsresult res = aEnc->Reset();

  if (NS_FAILED(res)) {
    printf("ERROR at %s.enc.Reset() code=0x%x.\n",aTestName,res);
    return res;
  }

  return res;
}

/**
 * A standard decoder test.
 */
nsresult standardDecoderTest(char * aTestName, char * aCharset, char * aSrc, 
  int32_t aSrcLen, char16_t * aRes, int32_t aResLen)
{
  printf("\n[%s] Unicode <- %s\n", aTestName, aCharset);

  // create converter
  CREATE_DECODER(aCharset);

  // test converter - easy test
  res = testDecoder(dec, aSrc, aSrcLen, aRes, aResLen, aTestName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetDecoder(dec, aTestName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressDecoder(dec, aSrc, aSrcLen, aRes, aResLen, aTestName);

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

nsresult loadBinaryFile(char * aFile, char * aBuff, int32_t * aBuffLen)
{
  FILE * f = fopen(aFile, "rb");
  if (!f) {
    printf("ERROR at opening file: \"%s\".\n", aFile);
    return NS_ERROR_UNEXPECTED;
  }

  int32_t n = fread(aBuff, 1, *aBuffLen, f);
  if (n >= *aBuffLen) {
    printf("ERROR at reading from file \"%s\": too much input data.\n", aFile);
    return NS_ERROR_UNEXPECTED;
  }

  *aBuffLen = n;
  fclose(f);
  return NS_OK;
}

nsresult loadUnicodeFile(char * aFile, char16_t * aBuff, int32_t * aBuffLen)
{
  int32_t buffLen = 2*(*aBuffLen);

  nsresult res = loadBinaryFile(aFile, (char *)aBuff, &buffLen);
  if (NS_FAILED(res)) return res;

  *aBuffLen = buffLen/2;
  return NS_OK;
}

nsresult testDecoderFromFiles(char * aCharset, char * aSrcFile, char * aResultFile)
{
  // create converter
  CREATE_DECODER(aCharset);

  int32_t srcLen = GENERAL_BUFFER;
  char src[GENERAL_BUFFER];
  int32_t expLen = GENERAL_BUFFER/2;
  char16_t exp[GENERAL_BUFFER/2];

  res = loadBinaryFile(aSrcFile, src, &srcLen);
  if (NS_FAILED(res)) return res;

  res = loadUnicodeFile(aResultFile, exp, &expLen);
  if (NS_FAILED(res)) return res;

  // test converter - easy test
  res = testDecoder(dec, src, srcLen, exp, expLen, "dec");

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }

  return NS_OK;
}

nsresult testEncoderFromFiles(char * aCharset, char * aSrcFile, char * aResultFile)
{
  // XXX write me
  return NS_OK;
}

//----------------------------------------------------------------------
// Decoders testing functions

/**
 * Test the ISO2022JP decoder.
 */
nsresult testISO2022JPDecoder()
{
  char * testName = "T102";
  printf("\n[%s] Unicode <- ISO2022JP\n", testName);

  // create converter
  CREATE_DECODER("iso-2022-jp");

  // test data
  char src[] = {"\x0d\x7f\xdd" "\x1b(J\xaa\xdc\x41" "\x1b$B\x21\x21" "\x1b$@\x32\x37" "\x1b(J\x1b(B\xcc"};
  char16_t exp[] = {0x000d,0x007f,0xfffd, 0xff6a,0xFF9C,0x0041, 0x3000, 0x5378, 0xfffd};

  // test converter - normal operation
  res = testDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetDecoder(dec, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the EUCJP decoder.
 */
nsresult testEUCJPDecoder()
{
  char * testName = "T103";
  printf("\n[%s] Unicode <- EUCJP\n", testName);

  // create converter
  CREATE_DECODER("euc-jp");

  // test data
  char src[] = {"\x45"};
  char16_t exp[] = {0x0045};

  // test converter - normal operation
  res = testDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetDecoder(dec, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the ISO88597 decoder.
 */
nsresult testISO88597Decoder()
{
  char * testName = "T104";
  printf("\n[%s] Unicode <- ISO88597\n", testName);

  // create converter
  CREATE_DECODER("iso-8859-7");

  // test data
  char src[] = {
    "\x09\x0d\x20\x40"
    "\x80\x98\xa3\xaf"
    "\xa7\xb1\xb3\xc9"
    "\xd9\xe3\xf4\xff"
  };
  char16_t exp[] = {
    0x0009, 0x000d, 0x0020, 0x0040, 
    0xfffd, 0xfffd, 0x00a3, 0x2015,
    0x00a7, 0x00b1, 0x00b3, 0x0399,
    0x03a9, 0x03b3, 0x03c4, 0xfffd
  };

  // test converter - normal operation
  res = testDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetDecoder(dec, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the SJIS decoder.
 */
nsresult testSJISDecoder()
{
  char * testName = "T105";
  printf("\n[%s] Unicode <- SJIS\n", testName);

  // create converter
  CREATE_DECODER("Shift_JIS");

  // test data
  char src[] = {
    "Japanese" /* English */
    "\x8a\xbf\x8e\x9a" /* Kanji */
    "\x83\x4a\x83\x5e\x83\x4a\x83\x69" /* Kantakana */
    "\x82\xd0\x82\xe7\x82\xaa\x82\xc8" /* Hiragana */
    "\x82\x50\x82\x51\x82\x52\x82\x60\x82\x61\x82\x62" /* full width 123ABC */
  };
  char16_t exp[] = {
    0x004A, 0x0061, 0x0070, 0x0061, 0x006E, 0x0065, 0x0073, 0x0065,
    0x6f22, 0x5b57,
    0x30ab, 0x30bf, 0x30ab, 0x30ca,
    0x3072, 0x3089, 0x304c, 0x306a,
    0xff11, 0xff12, 0xff13, 0xff21, 0xff22, 0xff23
  };

  // test converter - normal operation
  res = testDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetDecoder(dec, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the UTF8 decoder.
 */
nsresult testUTF8Decoder()
{
  char * testName = "T106";
  printf("\n[%s] Unicode <- UTF8\n", testName);

  // create converter
  CREATE_DECODER("utf-8");

#ifdef NOPE // XXX decomment this when I have test data
  // test data
  char src[] = {};
  char16_t exp[] = {};

  // test converter - normal operation
  res = testDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetDecoder(dec, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressDecoder(dec, src, ARRAY_SIZE(src)-1, exp, ARRAY_SIZE(exp), testName);
#endif

  // release converter
  NS_RELEASE(dec);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

//----------------------------------------------------------------------
// Encoders testing functions

/**
 * Test the Latin1 encoder.
 */
nsresult testLatin1Encoder()
{
  char * testName = "T201";
  printf("\n[%s] Unicode -> Latin1\n", testName);

  // create converter
  CREATE_ENCODER("iso-8859-1");
  enc->SetOutputErrorBehavior(enc->kOnError_Replace, nullptr, 0x00cc);

  // test data
  char16_t src[] = {0x0001,0x0002,0xffff,0x00e3};
  char exp[] = {"\x01\x02\xcc\xe3"};

  // test converter - easy test
  res = testEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetEncoder(enc, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // release converter
  NS_RELEASE(enc);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the Shift-JIS encoder.
 */
nsresult testSJISEncoder()
{
  char * testName = "T202";
  printf("\n[%s] Unicode -> SJIS\n", testName);

  // create converter
  CREATE_ENCODER("Shift_JIS");
  enc->SetOutputErrorBehavior(enc->kOnError_Replace, nullptr, 0x00cc);

  // test data
  char16_t src[] = {
    0x004A, 0x0061, 0x0070, 0x0061, 0x006E, 0x0065, 0x0073, 0x0065,
    0x6f22, 0x5b57,
    0x30ab, 0x30bf, 0x30ab, 0x30ca,
    0x3072, 0x3089, 0x304c, 0x306a,
    0xff11, 0xff12, 0xff13, 0xff21, 0xff22, 0xff23
  };
  char exp[] = {
    "Japanese" /* English */
    "\x8a\xbf\x8e\x9a" /* Kanji */
    "\x83\x4a\x83\x5e\x83\x4a\x83\x69" /* Kantakana */
    "\x82\xd0\x82\xe7\x82\xaa\x82\xc8" /* Hiragana */
    "\x82\x50\x82\x51\x82\x52\x82\x60\x82\x61\x82\x62" /* full width 123ABC */
  };

  // test converter - easy test
  res = testEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetEncoder(enc, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // release converter
  NS_RELEASE(enc);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the EUC-JP encoder.
 */
nsresult testEUCJPEncoder()
{
  char * testName = "T203";
  printf("\n[%s] Unicode -> EUCJP\n", testName);

  // create converter
  CREATE_ENCODER("euc-jp");
  enc->SetOutputErrorBehavior(enc->kOnError_Replace, nullptr, 0x00cc);

  // test data
  char16_t src[] = {0x0045, 0x0054};
  char exp[] = {"\x45\x54"};

  // test converter - easy test
  res = testEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetEncoder(enc, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // release converter
  NS_RELEASE(enc);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

/**
 * Test the ISO-2022-JP encoder.
 */
nsresult testISO2022JPEncoder()
{
  char * testName = "T204";
  printf("\n[%s] Unicode -> ISO2022JP\n", testName);

  // create converter
  CREATE_ENCODER("iso-2022-jp");
  enc->SetOutputErrorBehavior(enc->kOnError_Replace, nullptr, 0x00cc);

  // test data
  char16_t src[] = {0x000d,0x007f, 0xff6a,0xFF9C, 0x3000, 0x5378};
  char exp[] = {"\x0d\x7f" "\x1b(J\xaa\xdc" "\x1b$@\x21\x21\x32\x37\x1b(B"};

  // test converter - easy test
  res = testEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // reset converter
  if (NS_SUCCEEDED(res)) res = resetEncoder(enc, testName);

  // test converter - stress test
  if (NS_SUCCEEDED(res)) 
    res = testStressEncoder(enc, src, ARRAY_SIZE(src), exp, ARRAY_SIZE(exp)-1, testName);

  // release converter
  NS_RELEASE(enc);

  if (NS_FAILED(res)) {
    return res;
  } else {
    printf("Test Passed.\n");
    return NS_OK;
  }
}

nsresult  testPlatformCharset()
{
  nsIPlatformCharset *cinfo;
  nsresult res = CallGetService(kPlatformCharsetCID, &cinfo);
  if (NS_FAILED(res)) {
    printf("ERROR at GetService() code=0x%x.\n",res);
    return res;
  }

  nsString value;
  res = cinfo->GetCharset(kPlatformCharsetSel_PlainTextInClipboard , value);
  printf("Clipboard plain text encoding = %s\n", NS_LossyConvertUTF16toASCII(value).get());
  
  res = cinfo->GetCharset(kPlatformCharsetSel_FileName , value);
  printf("File Name encoding = %s\n", NS_LossyConvertUTF16toASCII(value).get());

  res = cinfo->GetCharset(kPlatformCharsetSel_Menu , value);
  printf("Menu encoding = %s\n", NS_LossyConvertUTF16toASCII(value).get());

  cinfo->Release();
  return res;
  
}

//----------------------------------------------------------------------
// Testing functions

nsresult testAll()
{
  nsresult res;

  // test the manager(s)
  res = testCharsetConverterManager();
  if (NS_FAILED(res)) return res;

  testPlatformCharset();

  // test decoders
  standardDecoderTest("T101", "ISO-8859-1", bLatin1_d0, bLatin1_s0, cLatin1_d0, cLatin1_s0);
  testISO2022JPDecoder();
  testEUCJPDecoder();
  testISO88597Decoder();
  testSJISDecoder();
  testUTF8Decoder();
  testMUTF7Decoder();
  testUTF7Decoder();

  // test encoders
  testLatin1Encoder();
  testSJISEncoder();
  testEUCJPEncoder();
  testISO2022JPEncoder();
  testMUTF7Encoder();
  testUTF7Encoder();

  // return
  return NS_OK;
}

nsresult testFromArgs(int argc, char **argv)
{
  nsresult res = NS_OK;
  if ((argc == 5) && (!strcmp(argv[1], "-tdec"))) {
    res = testDecoderFromFiles(argv[2], argv[3], argv[4]);
  } else if ((argc == 5) && (!strcmp(argv[1], "-tenc"))) {
    res = testEncoderFromFiles(argv[2], argv[3], argv[4]);
  } else {
    printf("Usage:\n");
    printf("  TestUConv.exe\n");
    printf("  TestUConv.exe -tdec encoding inputEncodedFile expectedResultUnicodeFile\n");
    printf("  TestUConv.exe -tenc encoding inputUnicodeFile expectedResultEncodedFile\n");
  }

  return res;
}

//----------------------------------------------------------------------
// Main program functions

nsresult init()
{
  nsresult rv = NS_InitXPCOM2(nullptr, nullptr, nullptr);
  if (NS_FAILED(rv))
    return rv;
  return CallGetService(kCharsetConverterManagerCID, &ccMan);
}

nsresult done()
{
  NS_RELEASE(ccMan);
  return NS_OK;
}

int main(int argc, char **argv)
{
  nsresult res;

  res = init();
  if (NS_FAILED(res)) return -1;

  if (argc <= 1) {
    printf("*** Unicode Converters Test ***\n");
    res = testAll();
    printf("\n***---------  Done  --------***\n");
  } else {
    res = testFromArgs(argc, argv);
  }

  done();

  if (NS_FAILED(res)) return -1;
  else return 0;
}
