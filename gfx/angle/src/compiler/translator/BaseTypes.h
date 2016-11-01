//
// Copyright (c) 2002-2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef COMPILER_TRANSLATOR_BASETYPES_H_
#define COMPILER_TRANSLATOR_BASETYPES_H_

#include <algorithm>
#include <array>

#include "common/debug.h"
#include "GLSLANG/ShaderLang.h"

//
// Precision qualifiers
//
enum TPrecision
{
    // These need to be kept sorted
    EbpUndefined,
    EbpLow,
    EbpMedium,
    EbpHigh,

    // end of list
    EbpLast
};

inline const char* getPrecisionString(TPrecision p)
{
    switch(p)
    {
    case EbpHigh:		return "highp";		break;
    case EbpMedium:		return "mediump";	break;
    case EbpLow:		return "lowp";		break;
    default:			return "mediump";   break;   // Safest fallback
    }
}

//
// Basic type.  Arrays, vectors, etc., are orthogonal to this.
//
enum TBasicType
{
    EbtVoid,
    EbtFloat,
    EbtInt,
    EbtUInt,
    EbtBool,
    EbtGVec4,              // non type: represents vec4, ivec4, and uvec4
    EbtGenType,            // non type: represents float, vec2, vec3, and vec4
    EbtGenIType,           // non type: represents int, ivec2, ivec3, and ivec4
    EbtGenUType,           // non type: represents uint, uvec2, uvec3, and uvec4
    EbtGenBType,           // non type: represents bool, bvec2, bvec3, and bvec4
    EbtVec,                // non type: represents vec2, vec3, and vec4
    EbtIVec,               // non type: represents ivec2, ivec3, and ivec4
    EbtUVec,               // non type: represents uvec2, uvec3, and uvec4
    EbtBVec,               // non type: represents bvec2, bvec3, and bvec4
    EbtGuardSamplerBegin,  // non type: see implementation of IsSampler()
    EbtSampler2D,
    EbtSampler3D,
    EbtSamplerCube,
    EbtSampler2DArray,
    EbtSamplerExternalOES,  // Only valid if OES_EGL_image_external exists.
    EbtSampler2DRect,       // Only valid if GL_ARB_texture_rectangle exists.
    EbtISampler2D,
    EbtISampler3D,
    EbtISamplerCube,
    EbtISampler2DArray,
    EbtUSampler2D,
    EbtUSampler3D,
    EbtUSamplerCube,
    EbtUSampler2DArray,
    EbtSampler2DShadow,
    EbtSamplerCubeShadow,
    EbtSampler2DArrayShadow,
    EbtGuardSamplerEnd,    // non type: see implementation of IsSampler()
    EbtGSampler2D,         // non type: represents sampler2D, isampler2D, and usampler2D
    EbtGSampler3D,         // non type: represents sampler3D, isampler3D, and usampler3D
    EbtGSamplerCube,       // non type: represents samplerCube, isamplerCube, and usamplerCube
    EbtGSampler2DArray,    // non type: represents sampler2DArray, isampler2DArray, and usampler2DArray
    EbtStruct,
    EbtInterfaceBlock,
    EbtAddress,            // should be deprecated??

    // end of list
    EbtLast
};

const char* getBasicString(TBasicType t);

inline bool IsSampler(TBasicType type)
{
    return type > EbtGuardSamplerBegin && type < EbtGuardSamplerEnd;
}

inline bool IsIntegerSampler(TBasicType type)
{
    switch (type)
    {
      case EbtISampler2D:
      case EbtISampler3D:
      case EbtISamplerCube:
      case EbtISampler2DArray:
      case EbtUSampler2D:
      case EbtUSampler3D:
      case EbtUSamplerCube:
      case EbtUSampler2DArray:
        return true;
      case EbtSampler2D:
      case EbtSampler3D:
      case EbtSamplerCube:
      case EbtSamplerExternalOES:
      case EbtSampler2DRect:
      case EbtSampler2DArray:
      case EbtSampler2DShadow:
      case EbtSamplerCubeShadow:
      case EbtSampler2DArrayShadow:
        return false;
      default:
        assert(!IsSampler(type));
    }

    return false;
}

inline bool IsSampler2D(TBasicType type)
{
    switch (type)
    {
      case EbtSampler2D:
      case EbtISampler2D:
      case EbtUSampler2D:
      case EbtSampler2DArray:
      case EbtISampler2DArray:
      case EbtUSampler2DArray:
      case EbtSampler2DRect:
      case EbtSamplerExternalOES:
      case EbtSampler2DShadow:
      case EbtSampler2DArrayShadow:
        return true;
      case EbtSampler3D:
      case EbtISampler3D:
      case EbtUSampler3D:
      case EbtISamplerCube:
      case EbtUSamplerCube:
      case EbtSamplerCube:
      case EbtSamplerCubeShadow:
        return false;
      default:
        assert(!IsSampler(type));
    }

    return false;
}

inline bool IsSamplerCube(TBasicType type)
{
    switch (type)
    {
      case EbtSamplerCube:
      case EbtISamplerCube:
      case EbtUSamplerCube:
      case EbtSamplerCubeShadow:
        return true;
      case EbtSampler2D:
      case EbtSampler3D:
      case EbtSamplerExternalOES:
      case EbtSampler2DRect:
      case EbtSampler2DArray:
      case EbtISampler2D:
      case EbtISampler3D:
      case EbtISampler2DArray:
      case EbtUSampler2D:
      case EbtUSampler3D:
      case EbtUSampler2DArray:
      case EbtSampler2DShadow:
      case EbtSampler2DArrayShadow:
        return false;
      default:
        assert(!IsSampler(type));
    }

    return false;
}

inline bool IsSampler3D(TBasicType type)
{
    switch (type)
    {
      case EbtSampler3D:
      case EbtISampler3D:
      case EbtUSampler3D:
        return true;
      case EbtSampler2D:
      case EbtSamplerCube:
      case EbtSamplerExternalOES:
      case EbtSampler2DRect:
      case EbtSampler2DArray:
      case EbtISampler2D:
      case EbtISamplerCube:
      case EbtISampler2DArray:
      case EbtUSampler2D:
      case EbtUSamplerCube:
      case EbtUSampler2DArray:
      case EbtSampler2DShadow:
      case EbtSamplerCubeShadow:
      case EbtSampler2DArrayShadow:
        return false;
      default:
        assert(!IsSampler(type));
    }

    return false;
}

inline bool IsSamplerArray(TBasicType type)
{
    switch (type)
    {
      case EbtSampler2DArray:
      case EbtISampler2DArray:
      case EbtUSampler2DArray:
      case EbtSampler2DArrayShadow:
        return true;
      case EbtSampler2D:
      case EbtISampler2D:
      case EbtUSampler2D:
      case EbtSampler2DRect:
      case EbtSamplerExternalOES:
      case EbtSampler3D:
      case EbtISampler3D:
      case EbtUSampler3D:
      case EbtISamplerCube:
      case EbtUSamplerCube:
      case EbtSamplerCube:
      case EbtSampler2DShadow:
      case EbtSamplerCubeShadow:
        return false;
      default:
        assert(!IsSampler(type));
    }

    return false;
}

inline bool IsShadowSampler(TBasicType type)
{
    switch (type)
    {
      case EbtSampler2DShadow:
      case EbtSamplerCubeShadow:
      case EbtSampler2DArrayShadow:
        return true;
      case EbtISampler2D:
      case EbtISampler3D:
      case EbtISamplerCube:
      case EbtISampler2DArray:
      case EbtUSampler2D:
      case EbtUSampler3D:
      case EbtUSamplerCube:
      case EbtUSampler2DArray:
      case EbtSampler2D:
      case EbtSampler3D:
      case EbtSamplerCube:
      case EbtSamplerExternalOES:
      case EbtSampler2DRect:
      case EbtSampler2DArray:
        return false;
      default:
        assert(!IsSampler(type));
    }

    return false;
}

inline bool IsInteger(TBasicType type)
{
    return type == EbtInt || type == EbtUInt;
}

inline bool SupportsPrecision(TBasicType type)
{
    return type == EbtFloat || type == EbtInt || type == EbtUInt || IsSampler(type);
}

//
// Qualifiers and built-ins.  These are mainly used to see what can be read
// or written, and by the machine dependent translator to know which registers
// to allocate variables in.  Since built-ins tend to go to different registers
// than varying or uniform, it makes sense they are peers, not sub-classes.
//
enum TQualifier
{
    EvqTemporary,   // For temporaries (within a function), read/write
    EvqGlobal,      // For globals read/write
    EvqConst,       // User defined constants and non-output parameters in functions
    EvqAttribute,   // Readonly
    EvqVaryingIn,   // readonly, fragment shaders only
    EvqVaryingOut,  // vertex shaders only  read/write
    EvqUniform,     // Readonly, vertex and fragment

    EvqVertexIn,     // Vertex shader input
    EvqFragmentOut,  // Fragment shader output
    EvqVertexOut,    // Vertex shader output
    EvqFragmentIn,   // Fragment shader input

    // parameters
    EvqIn,
    EvqOut,
    EvqInOut,
    EvqConstReadOnly,

    // built-ins read by vertex shader
    EvqInstanceID,
    EvqVertexID,

    // built-ins written by vertex shader
    EvqPosition,
    EvqPointSize,

    // built-ins read by fragment shader
    EvqFragCoord,
    EvqFrontFacing,
    EvqPointCoord,

    // built-ins written by fragment shader
    EvqFragColor,
    EvqFragData,

    EvqFragDepth,     // gl_FragDepth for ESSL300.
    EvqFragDepthEXT,  // gl_FragDepthEXT for ESSL100, EXT_frag_depth.

    EvqSecondaryFragColorEXT,  // EXT_blend_func_extended
    EvqSecondaryFragDataEXT,   // EXT_blend_func_extended

    // built-ins written by the shader_framebuffer_fetch extension(s)
    EvqLastFragColor,
    EvqLastFragData,

    // GLSL ES 3.0 vertex output and fragment input
    EvqSmooth,    // Incomplete qualifier, smooth is the default
    EvqFlat,      // Incomplete qualifier
    EvqCentroid,  // Incomplete qualifier
    EvqSmoothOut,
    EvqFlatOut,
    EvqCentroidOut,  // Implies smooth
    EvqSmoothIn,
    EvqFlatIn,
    EvqCentroidIn,  // Implies smooth

    // GLSL ES 3.1 compute shader special variables
    EvqComputeIn,
    EvqNumWorkGroups,
    EvqWorkGroupSize,
    EvqWorkGroupID,
    EvqLocalInvocationID,
    EvqGlobalInvocationID,
    EvqLocalInvocationIndex,

    // end of list
    EvqLast
};

inline bool IsQualifierUnspecified(TQualifier qualifier)
{
    return (qualifier == EvqTemporary || qualifier == EvqGlobal);
}

enum TLayoutMatrixPacking
{
    EmpUnspecified,
    EmpRowMajor,
    EmpColumnMajor
};

enum TLayoutBlockStorage
{
    EbsUnspecified,
    EbsShared,
    EbsPacked,
    EbsStd140
};

struct TLayoutQualifier
{
    int location;
    unsigned int locationsSpecified;
    TLayoutMatrixPacking matrixPacking;
    TLayoutBlockStorage blockStorage;

    // Compute shader layout qualifiers.
    sh::WorkGroupSize localSize;

    static TLayoutQualifier create()
    {
        TLayoutQualifier layoutQualifier;

        layoutQualifier.location = -1;
        layoutQualifier.locationsSpecified = 0;
        layoutQualifier.matrixPacking = EmpUnspecified;
        layoutQualifier.blockStorage = EbsUnspecified;

        layoutQualifier.localSize.fill(-1);

        return layoutQualifier;
    }

    bool isEmpty() const
    {
        return location == -1 && matrixPacking == EmpUnspecified &&
               blockStorage == EbsUnspecified && !localSize.isAnyValueSet();
    }

    bool isCombinationValid() const
    {
        bool workSizeSpecified = localSize.isAnyValueSet();
        bool otherLayoutQualifiersSpecified =
            (location != -1 || matrixPacking != EmpUnspecified || blockStorage != EbsUnspecified);

        // we can have either the work group size specified, or the other layout qualifiers
        return !(workSizeSpecified && otherLayoutQualifiersSpecified);
    }

    bool isLocalSizeEqual(const sh::WorkGroupSize &localSizeIn) const
    {
        return localSize.isWorkGroupSizeMatching(localSizeIn);
    }
};

inline const char *getWorkGroupSizeString(size_t dimension)
{
    switch (dimension)
    {
        case 0u:
            return "local_size_x";
        case 1u:
            return "local_size_y";
        case 2u:
            return "local_size_z";
        default:
            UNREACHABLE();
            return "dimension out of bounds";
    }
}

//
// This is just for debug print out, carried along with the definitions above.
//
inline const char* getQualifierString(TQualifier q)
{
    // clang-format off
    switch(q)
    {
    case EvqTemporary:              return "Temporary";
    case EvqGlobal:                 return "Global";
    case EvqConst:                  return "const";
    case EvqAttribute:              return "attribute";
    case EvqVaryingIn:              return "varying";
    case EvqVaryingOut:             return "varying";
    case EvqUniform:                return "uniform";
    case EvqVertexIn:               return "in";
    case EvqFragmentOut:            return "out";
    case EvqVertexOut:              return "out";
    case EvqFragmentIn:             return "in";
    case EvqIn:                     return "in";
    case EvqOut:                    return "out";
    case EvqInOut:                  return "inout";
    case EvqConstReadOnly:          return "const";
    case EvqInstanceID:             return "InstanceID";
    case EvqVertexID:               return "VertexID";
    case EvqPosition:               return "Position";
    case EvqPointSize:              return "PointSize";
    case EvqFragCoord:              return "FragCoord";
    case EvqFrontFacing:            return "FrontFacing";
    case EvqPointCoord:             return "PointCoord";
    case EvqFragColor:              return "FragColor";
    case EvqFragData:               return "FragData";
    case EvqFragDepthEXT:           return "FragDepth";
    case EvqFragDepth:              return "FragDepth";
    case EvqSecondaryFragColorEXT:  return "SecondaryFragColorEXT";
    case EvqSecondaryFragDataEXT:   return "SecondaryFragDataEXT";
    case EvqLastFragColor:          return "LastFragColor";
    case EvqLastFragData:           return "LastFragData";
    case EvqSmoothOut:              return "smooth out";
    case EvqCentroidOut:            return "smooth centroid out";
    case EvqFlatOut:                return "flat out";
    case EvqSmoothIn:               return "smooth in";
    case EvqFlatIn:                 return "flat in";
    case EvqCentroidIn:             return "smooth centroid in";
    case EvqCentroid:               return "centroid";
    case EvqFlat:                   return "flat";
    case EvqSmooth:                 return "smooth";
    case EvqComputeIn:              return "in";
    case EvqNumWorkGroups:          return "NumWorkGroups";
    case EvqWorkGroupSize:          return "WorkGroupSize";
    case EvqWorkGroupID:            return "WorkGroupID";
    case EvqLocalInvocationID:      return "LocalInvocationID";
    case EvqGlobalInvocationID:     return "GlobalInvocationID";
    case EvqLocalInvocationIndex:   return "LocalInvocationIndex";
    default: UNREACHABLE();         return "unknown qualifier";
    }
    // clang-format on
}

inline const char* getMatrixPackingString(TLayoutMatrixPacking mpq)
{
    switch (mpq)
    {
    case EmpUnspecified:    return "mp_unspecified";
    case EmpRowMajor:       return "row_major";
    case EmpColumnMajor:    return "column_major";
    default: UNREACHABLE(); return "unknown matrix packing";
    }
}

inline const char* getBlockStorageString(TLayoutBlockStorage bsq)
{
    switch (bsq)
    {
    case EbsUnspecified:    return "bs_unspecified";
    case EbsShared:         return "shared";
    case EbsPacked:         return "packed";
    case EbsStd140:         return "std140";
    default: UNREACHABLE(); return "unknown block storage";
    }
}

inline const char* getInterpolationString(TQualifier q)
{
    switch(q)
    {
    case EvqSmoothOut:      return "smooth";   break;
    case EvqCentroidOut:    return "smooth centroid"; break;
    case EvqFlatOut:        return "flat";     break;
    case EvqSmoothIn:       return "smooth";   break;
    case EvqCentroidIn:     return "smooth centroid"; break;
    case EvqFlatIn:         return "flat";     break;
    default: UNREACHABLE(); return "unknown interpolation";
    }
}

#endif // COMPILER_TRANSLATOR_BASETYPES_H_
