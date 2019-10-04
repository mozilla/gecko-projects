/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_FT2FONTLIST_H
#define GFX_FT2FONTLIST_H

#include "mozilla/MemoryReporting.h"
#include "gfxFT2FontBase.h"
#include "gfxPlatformFontList.h"

namespace mozilla {
namespace dom {
class FontListEntry;
};
};  // namespace mozilla
using mozilla::dom::FontListEntry;

class FontNameCache;
typedef struct FT_FaceRec_* FT_Face;
class nsZipArchive;
class WillShutdownObserver;
class FTUserFontData;

class FT2FontEntry : public gfxFT2FontEntryBase {
 public:
  explicit FT2FontEntry(const nsACString& aFaceName)
      : gfxFT2FontEntryBase(aFaceName), mFTFontIndex(0) {}

  ~FT2FontEntry();

  gfxFontEntry* Clone() const override;

  const nsCString& GetName() const { return Name(); }

  // create a font entry for a downloaded font
  static FT2FontEntry* CreateFontEntry(
      const nsACString& aFontName, WeightRange aWeight, StretchRange aStretch,
      SlantStyleRange aStyle, const uint8_t* aFontData, uint32_t aLength);

  // create a font entry representing an installed font, identified by
  // a FontListEntry; the freetype and cairo faces will not be instantiated
  // until actually needed
  static FT2FontEntry* CreateFontEntry(const FontListEntry& aFLE);

  // Create a font entry for a given freetype face; if it is an installed font,
  // also record the filename and index.
  // aFontData (if non-nullptr) is NS_Malloc'ed data that aFace depends on,
  // to be freed after the face is destroyed.
  // aLength is the length of aFontData.
  static FT2FontEntry* CreateFontEntry(FT_Face, const char* aFilename,
                                       uint8_t aIndex, const nsACString& aName);

  gfxFont* CreateFontInstance(const gfxFontStyle* aStyle) override;

  nsresult ReadCMAP(FontInfoData* aFontInfoData = nullptr) override;

  hb_blob_t* GetFontTable(uint32_t aTableTag) override;

  nsresult CopyFontTable(uint32_t aTableTag,
                         nsTArray<uint8_t>& aBuffer) override;

  bool HasVariations() override;
  void GetVariationAxes(
      nsTArray<gfxFontVariationAxis>& aVariationAxes) override;
  void GetVariationInstances(
      nsTArray<gfxFontVariationInstance>& aInstances) override;

  // Check for various kinds of brokenness, and set flags on the entry
  // accordingly so that we avoid using bad font tables
  void CheckForBrokenFont(gfxFontFamily* aFamily);
  void CheckForBrokenFont(const nsACString& aFamilyKey);

  already_AddRefed<mozilla::gfx::SharedFTFace> GetFTFace(bool aCommit = false);
  FTUserFontData* GetUserFontData();

  FT_MM_Var* GetMMVar() override;

  /**
   * Append this face's metadata to aFaceList for storage in the FontNameCache
   * (for faster startup).
   * The aPSName and aFullName parameters here can in principle be empty,
   * but if they are missing for a given face then src:local() lookups will
   * not be able to find it when the shared font list is in use.
   */
  void AppendToFaceList(nsCString& aFaceList, const nsACString& aFamilyName,
                        const nsACString& aPSName, const nsACString& aFullName);

  void AddSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const override;
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              FontListSizes* aSizes) const override;

  RefPtr<mozilla::gfx::SharedFTFace> mFTFace;

  FT_MM_Var* mMMVar = nullptr;

  nsCString mFilename;
  uint8_t mFTFontIndex;

  mozilla::ThreadSafeWeakPtr<mozilla::gfx::UnscaledFontFreeType> mUnscaledFont;

  bool mHasVariations = false;
  bool mHasVariationsInitialized = false;
  bool mMMVarInitialized = false;
};

class FT2FontFamily : public gfxFontFamily {
 public:
  explicit FT2FontFamily(const nsACString& aName) : gfxFontFamily(aName) {}

  // Append this family's faces to the IPC fontlist
  void AddFacesToFontList(nsTArray<FontListEntry>* aFontList);
};

class gfxFT2FontList : public gfxPlatformFontList {
 public:
  gfxFT2FontList();
  virtual ~gfxFT2FontList();

  gfxFontEntry* CreateFontEntry(
      mozilla::fontlist::Face* aFace,
      const mozilla::fontlist::Family* aFamily) override;

  gfxFontEntry* LookupLocalFont(const nsACString& aFontName,
                                WeightRange aWeightForEntry,
                                StretchRange aStretchForEntry,
                                SlantStyleRange aStyleForEntry) override;

  gfxFontEntry* MakePlatformFont(const nsACString& aFontName,
                                 WeightRange aWeightForEntry,
                                 StretchRange aStretchForEntry,
                                 SlantStyleRange aStyleForEntry,
                                 const uint8_t* aFontData,
                                 uint32_t aLength) override;

  void WriteCache();

  void GetSystemFontList(nsTArray<FontListEntry>* retValue);

  static gfxFT2FontList* PlatformFontList() {
    return static_cast<gfxFT2FontList*>(
        gfxPlatformFontList::PlatformFontList());
  }

  gfxFontFamily* CreateFontFamily(const nsACString& aName) const override;

  void WillShutdown();

 protected:
  typedef enum { kUnknown, kStandard } StandardFile;

  // initialize font lists
  nsresult InitFontListForPlatform() override;

  void AppendFaceFromFontListEntry(const FontListEntry& aFLE,
                                   StandardFile aStdFile);

  void AppendFacesFromFontFile(const nsCString& aFileName,
                               FontNameCache* aCache, StandardFile aStdFile);

  void AppendFacesFromOmnijarEntry(nsZipArchive* aReader,
                                   const nsCString& aEntryName,
                                   FontNameCache* aCache, bool aJarChanged);

  void InitSharedFontListForPlatform() override;
  void CollectInitData(const FontListEntry& aFLE, const nsCString& aPSName,
                       const nsCString& aFullName, StandardFile aStdFile);

  /**
   * Callback passed to AppendFacesFromCachedFaceList to collect family/face
   * information in either the unshared or shared list we're building.
   */
  typedef void (*CollectFunc)(const FontListEntry& aFLE,
                              const nsCString& aPSName,
                              const nsCString& aFullName,
                              StandardFile aStdFile);

  /**
   * Append faces from the face-list record for a specific file.
   * aCollectFace is a callback that will store the face(s) in either the
   * unshared mFontFamilies list or the mFamilyInitData/mFaceInitData tables
   * that will be used to initialize the shared list.
   * Returns true if it is able to read at least one face entry; false if no
   * usable face entry was found.
   */
  bool AppendFacesFromCachedFaceList(CollectFunc aCollectFace,
                                     const nsCString& aFileName,
                                     const nsCString& aFaceList,
                                     StandardFile aStdFile);

  void AddFaceToList(const nsCString& aEntryName, uint32_t aIndex,
                     StandardFile aStdFile, FT_Face aFace,
                     nsCString& aFaceList);

  void FindFonts();

  void FindFontsInOmnijar(FontNameCache* aCache);

  void FindFontsInDir(const nsCString& aDir, FontNameCache* aFNC);

  FontFamily GetDefaultFontForPlatform(const gfxFontStyle* aStyle) override;

  nsTHashtable<nsCStringHashKey> mSkipSpaceLookupCheckFamilies;

 private:
  mozilla::UniquePtr<FontNameCache> mFontNameCache;
  int64_t mJarModifiedTime;
  RefPtr<WillShutdownObserver> mObserver;

  nsTArray<mozilla::fontlist::Family::InitData> mFamilyInitData;
  nsClassHashtable<nsCStringHashKey,
                   nsTArray<mozilla::fontlist::Face::InitData>>
      mFaceInitData;
};

#endif /* GFX_FT2FONTLIST_H */
