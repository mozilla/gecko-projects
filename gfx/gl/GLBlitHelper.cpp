/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxUtils.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLScreenBuffer.h"
#include "ScopedGLHelpers.h"
#include "mozilla/Preferences.h"
#include "ImageContainer.h"
#include "HeapCopyOfStackArray.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/UniquePtr.h"
#include "GPUVideoImage.h"

#ifdef MOZ_WIDGET_ANDROID
#include "AndroidSurfaceTexture.h"
#include "GLImages.h"
#include "GLLibraryEGL.h"
#endif

#ifdef XP_MACOSX
#include "MacIOSurfaceImage.h"
#include "GLContextCGL.h"
#endif

using mozilla::layers::PlanarYCbCrImage;
using mozilla::layers::PlanarYCbCrData;

namespace mozilla {
namespace gl {

// --

const char* const kFragHeader_Tex2D = "\
    #define SAMPLER sampler2D                                                \n\
    #if __VERSION__ >= 130                                                   \n\
        #define TEXTURE texture                                              \n\
    #else                                                                    \n\
        #define TEXTURE texture2D                                            \n\
    #endif                                                                   \n\
";
const char* const kFragHeader_Tex2DRect = "\
    #define SAMPLER sampler2DRect                                            \n\
    #if __VERSION__ >= 130                                                   \n\
        #define TEXTURE texture                                              \n\
    #else                                                                    \n\
        #define TEXTURE texture2DRect                                        \n\
    #endif                                                                   \n\
";
const char* const kFragHeader_TexExt = "\
    #extension GL_OES_EGL_image_external : require                           \n\
    #define SAMPLER samplerExternalOES                                       \n\
    #define TEXTURE texture2D                                                \n\
";

const char* const kFragBody_RGBA = "\
    VARYING vec2 vTexCoord0;                                                 \n\
    uniform SAMPLER uTex0;                                                   \n\
                                                                             \n\
    void main(void)                                                          \n\
    {                                                                        \n\
        FRAG_COLOR = TEXTURE(uTex0, vTexCoord0);                             \n\
    }                                                                        \n\
";
const char* const kFragBody_CrYCb = "\
    VARYING vec2 vTexCoord0;                                                 \n\
    uniform SAMPLER uTex0;                                                   \n\
    uniform mat4 uColorMatrix;                                               \n\
                                                                             \n\
    void main(void)                                                          \n\
    {                                                                        \n\
        vec4 yuv = vec4(TEXTURE(uTex0, vTexCoord0).gbr,                      \n\
                        1.0);                                                \n\
        vec4 rgb = uColorMatrix * yuv;                                       \n\
        FRAG_COLOR = vec4(rgb.rgb, 1.0);                                     \n\
    }                                                                        \n\
";
const char* const kFragBody_NV12 = "\
    VARYING vec2 vTexCoord0;                                                 \n\
    VARYING vec2 vTexCoord1;                                                 \n\
    uniform SAMPLER uTex0;                                                   \n\
    uniform SAMPLER uTex1;                                                   \n\
    uniform mat4 uColorMatrix;                                               \n\
                                                                             \n\
    void main(void)                                                          \n\
    {                                                                        \n\
        vec4 yuv = vec4(TEXTURE(uTex0, vTexCoord0).x,                        \n\
                        TEXTURE(uTex1, vTexCoord1).xy,                       \n\
                        1.0);                                                \n\
        vec4 rgb = uColorMatrix * yuv;                                       \n\
        FRAG_COLOR = vec4(rgb.rgb, 1.0);                                     \n\
    }                                                                        \n\
";
const char* const kFragBody_PlanarYUV = "\
    VARYING vec2 vTexCoord0;                                                 \n\
    VARYING vec2 vTexCoord1;                                                 \n\
    uniform SAMPLER uTex0;                                                   \n\
    uniform SAMPLER uTex1;                                                   \n\
    uniform SAMPLER uTex2;                                                   \n\
    uniform mat4 uColorMatrix;                                               \n\
                                                                             \n\
    void main(void)                                                          \n\
    {                                                                        \n\
        vec4 yuv = vec4(TEXTURE(uTex0, vTexCoord0).x,                        \n\
                        TEXTURE(uTex1, vTexCoord1).x,                        \n\
                        TEXTURE(uTex2, vTexCoord1).x,                        \n\
                        1.0);                                                \n\
        vec4 rgb = uColorMatrix * yuv;                                       \n\
        FRAG_COLOR = vec4(rgb.rgb, 1.0);                                     \n\
    }                                                                        \n\
";

// --

ScopedSaveMultiTex::ScopedSaveMultiTex(GLContext* const gl, const uint8_t texCount,
                                       const GLenum texTarget)
    : mGL(*gl)
    , mTexCount(texCount)
    , mTexTarget(texTarget)
    , mOldTexUnit(mGL.GetIntAs<GLenum>(LOCAL_GL_ACTIVE_TEXTURE))
{
    GLenum texBinding;
    switch (mTexTarget) {
    case LOCAL_GL_TEXTURE_2D:
        texBinding = LOCAL_GL_TEXTURE_BINDING_2D;
        break;
    case LOCAL_GL_TEXTURE_RECTANGLE:
        texBinding = LOCAL_GL_TEXTURE_BINDING_RECTANGLE;
        break;
    case LOCAL_GL_TEXTURE_EXTERNAL:
        texBinding = LOCAL_GL_TEXTURE_BINDING_EXTERNAL;
        break;
    default:
        gfxCriticalError() << "Unhandled texTarget: " << texTarget;
    }

    for (uint8_t i = 0; i < mTexCount; i++) {
        mGL.fActiveTexture(LOCAL_GL_TEXTURE0 + i);
        if (mGL.IsSupported(GLFeature::sampler_objects)) {
            mOldTexSampler[i] = mGL.GetIntAs<GLuint>(LOCAL_GL_SAMPLER_BINDING);
            mGL.fBindSampler(i, 0);
        }
        mOldTex[i] = mGL.GetIntAs<GLuint>(texBinding);
    }
}

ScopedSaveMultiTex::~ScopedSaveMultiTex()
{
    for (uint8_t i = 0; i < mTexCount; i++) {
        mGL.fActiveTexture(LOCAL_GL_TEXTURE0 + i);
        if (mGL.IsSupported(GLFeature::sampler_objects)) {
            mGL.fBindSampler(i, mOldTexSampler[i]);
        }
        mGL.fBindTexture(mTexTarget, mOldTex[i]);
    }
    mGL.fActiveTexture(mOldTexUnit);
}

// --

class ScopedBindArrayBuffer final
{
    GLContext& mGL;
    const GLuint mOldVBO;

public:
    ScopedBindArrayBuffer(GLContext* const gl, const GLuint vbo)
        : mGL(*gl)
        , mOldVBO(mGL.GetIntAs<GLuint>(LOCAL_GL_ARRAY_BUFFER_BINDING))
    {
        mGL.fBindBuffer(LOCAL_GL_ARRAY_BUFFER, vbo);
    }

    ~ScopedBindArrayBuffer()
    {
        mGL.fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mOldVBO);
    }
};

// --

class ScopedShader final
{
    GLContext& mGL;
    const GLuint mName;

public:
    ScopedShader(GLContext* const gl, const GLenum shaderType)
        : mGL(*gl)
        , mName(mGL.fCreateShader(shaderType))
    { }

    ~ScopedShader()
    {
        mGL.fDeleteShader(mName);
    }

    operator GLuint() const { return mName; }
};

// --

class SaveRestoreCurrentProgram final
{
    GLContext& mGL;
    const GLuint mOld;

public:
    explicit SaveRestoreCurrentProgram(GLContext* const gl)
        : mGL(*gl)
        , mOld(mGL.GetIntAs<GLuint>(LOCAL_GL_CURRENT_PROGRAM))
    { }

    ~SaveRestoreCurrentProgram()
    {
        mGL.fUseProgram(mOld);
    }
};

// --

class ScopedDrawBlitState final
{
    GLContext& mGL;

    const bool blend;
    const bool cullFace;
    const bool depthTest;
    const bool dither;
    const bool polyOffsFill;
    const bool sampleAToC;
    const bool sampleCover;
    const bool scissor;
    const bool stencil;
    Maybe<bool> rasterizerDiscard;

    realGLboolean colorMask[4];
    GLint viewport[4];

public:
    ScopedDrawBlitState(GLContext* const gl, const gfx::IntSize& destSize)
        : mGL(*gl)
        , blend       (mGL.PushEnabled(LOCAL_GL_BLEND,                    false))
        , cullFace    (mGL.PushEnabled(LOCAL_GL_CULL_FACE,                false))
        , depthTest   (mGL.PushEnabled(LOCAL_GL_DEPTH_TEST,               false))
        , dither      (mGL.PushEnabled(LOCAL_GL_DITHER,                   true))
        , polyOffsFill(mGL.PushEnabled(LOCAL_GL_POLYGON_OFFSET_FILL,      false))
        , sampleAToC  (mGL.PushEnabled(LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE, false))
        , sampleCover (mGL.PushEnabled(LOCAL_GL_SAMPLE_COVERAGE,          false))
        , scissor     (mGL.PushEnabled(LOCAL_GL_SCISSOR_TEST,             false))
        , stencil     (mGL.PushEnabled(LOCAL_GL_STENCIL_TEST,             false))
    {
        if (mGL.IsSupported(GLFeature::transform_feedback2)) {
            // Technically transform_feedback2 requires transform_feedback, which actually
            // adds RASTERIZER_DISCARD.
            rasterizerDiscard = Some(mGL.PushEnabled(LOCAL_GL_RASTERIZER_DISCARD, false));
        }

        mGL.fGetBooleanv(LOCAL_GL_COLOR_WRITEMASK, colorMask);
        mGL.fColorMask(true, true, true, true);

        mGL.fGetIntegerv(LOCAL_GL_VIEWPORT, viewport);
        mGL.fViewport(0, 0, destSize.width, destSize.height);
    }

    ~ScopedDrawBlitState()
    {
        mGL.SetEnabled(LOCAL_GL_BLEND,                    blend       );
        mGL.SetEnabled(LOCAL_GL_CULL_FACE,                cullFace    );
        mGL.SetEnabled(LOCAL_GL_DEPTH_TEST,               depthTest   );
        mGL.SetEnabled(LOCAL_GL_DITHER,                   dither      );
        mGL.SetEnabled(LOCAL_GL_POLYGON_OFFSET_FILL,      polyOffsFill);
        mGL.SetEnabled(LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE, sampleAToC  );
        mGL.SetEnabled(LOCAL_GL_SAMPLE_COVERAGE,          sampleCover );
        mGL.SetEnabled(LOCAL_GL_SCISSOR_TEST,             scissor     );
        mGL.SetEnabled(LOCAL_GL_STENCIL_TEST,             stencil     );
        if (rasterizerDiscard) {
            mGL.SetEnabled(LOCAL_GL_RASTERIZER_DISCARD, rasterizerDiscard.value());
        }

        mGL.fColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        mGL.fViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

// --

DrawBlitProg::DrawBlitProg(const GLBlitHelper* const parent, const GLuint prog)
    : mParent(*parent)
    , mProg(prog)
    , mLoc_u1ForYFlip(mParent.mGL->fGetUniformLocation(mProg, "u1ForYFlip"))
    , mLoc_uSrcRect(mParent.mGL->fGetUniformLocation(mProg, "uSrcRect"))
    , mLoc_uTexSize0(mParent.mGL->fGetUniformLocation(mProg, "uTexSize0"))
    , mLoc_uTexSize1(mParent.mGL->fGetUniformLocation(mProg, "uTexSize1"))
    , mLoc_uDivisors(mParent.mGL->fGetUniformLocation(mProg, "uDivisors"))
    , mLoc_uColorMatrix(mParent.mGL->fGetUniformLocation(mProg, "uColorMatrix"))
{
    MOZ_ASSERT(mLoc_u1ForYFlip != -1);
    MOZ_ASSERT(mLoc_uSrcRect != -1);
    MOZ_ASSERT(mLoc_uTexSize0 != -1);
    if (mLoc_uColorMatrix != -1) {
        MOZ_ASSERT(mLoc_uTexSize1 != -1);
        MOZ_ASSERT(mLoc_uDivisors != -1);
    }
}

DrawBlitProg::~DrawBlitProg()
{
    const auto& gl = mParent.mGL;
    if (!gl->MakeCurrent())
        return;

    gl->fDeleteProgram(mProg);
}

void
DrawBlitProg::Draw(const BaseArgs& args, const YUVArgs* const argsYUV) const
{
    const auto& gl = mParent.mGL;

    const SaveRestoreCurrentProgram oldProg(gl);
    gl->fUseProgram(mProg);

    // --

    gl->fUniform1f(mLoc_u1ForYFlip, args.yFlip ? 1 : 0);
    gl->fUniform4f(mLoc_uSrcRect,
                   args.srcRect.x, args.srcRect.y,
                   args.srcRect.width, args.srcRect.height);
    gl->fUniform2f(mLoc_uTexSize0, args.texSize0.width, args.texSize0.height);

    MOZ_ASSERT(bool(argsYUV) == (mLoc_uColorMatrix != -1));
    if (argsYUV) {
        gl->fUniform2f(mLoc_uTexSize1, argsYUV->texSize1.width, argsYUV->texSize1.height);
        gl->fUniform2f(mLoc_uDivisors, argsYUV->divisors.width, argsYUV->divisors.height);
        const auto& colorMatrix = gfxUtils::YuvToRgbMatrix4x4ColumnMajor(argsYUV->colorSpace);
        gl->fUniformMatrix4fv(mLoc_uColorMatrix, 1, false, colorMatrix);
    }

    // --

    const ScopedDrawBlitState drawState(gl, args.destSize);

    GLuint oldVAO;
    GLint vaa0Enabled;
    GLint vaa0Size;
    GLenum vaa0Type;
    GLint vaa0Normalized;
    GLsizei vaa0Stride;
    GLvoid* vaa0Pointer;
    if (mParent.mQuadVAO) {
        oldVAO = gl->GetIntAs<GLuint>(LOCAL_GL_VERTEX_ARRAY_BINDING);
        gl->fBindVertexArray(mParent.mQuadVAO);
    } else {
        gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vaa0Enabled);
        gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_SIZE, &vaa0Size);
        gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_TYPE, (GLint*)&vaa0Type);
        gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &vaa0Normalized);
        gl->fGetVertexAttribiv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_STRIDE, (GLint*)&vaa0Stride);
        gl->fGetVertexAttribPointerv(0, LOCAL_GL_VERTEX_ATTRIB_ARRAY_POINTER, &vaa0Pointer);

        gl->fEnableVertexAttribArray(0);
        const ScopedBindArrayBuffer bindVBO(gl, mParent.mQuadVBO);
        gl->fVertexAttribPointer(0, 2, LOCAL_GL_FLOAT, false, 0, 0);
    }

    gl->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 4);

    if (mParent.mQuadVAO) {
        gl->fBindVertexArray(oldVAO);
    } else {
        if (vaa0Enabled) {
            gl->fEnableVertexAttribArray(0);
        } else {
            gl->fDisableVertexAttribArray(0);
        }
        gl->fVertexAttribPointer(0, vaa0Size, vaa0Type, bool(vaa0Normalized), vaa0Stride,
                                 vaa0Pointer);
    }
}

// --

GLBlitHelper::GLBlitHelper(GLContext* const gl)
    : mGL(gl)
    , mQuadVAO(0)
    , mQuadVBO(0)
    , mDrawBlitProg_VertShader(mGL->fCreateShader(LOCAL_GL_VERTEX_SHADER))
    , mYuvUploads{0}
    , mYuvUploads_YSize(0, 0)
    , mYuvUploads_UVSize(0, 0)
{
    mGL->fGenBuffers(1, &mQuadVBO);
    {
        const ScopedBindArrayBuffer bindVBO(mGL, mQuadVBO);

        const float quadData[] = {
            0, 0,
            1, 0,
            0, 1,
            1, 1
        };
        const HeapCopyOfStackArray<float> heapQuadData(quadData);
        mGL->fBufferData(LOCAL_GL_ARRAY_BUFFER, heapQuadData.ByteLength(),
                         heapQuadData.Data(), LOCAL_GL_STATIC_DRAW);

        if (mGL->IsSupported(GLFeature::vertex_array_object)) {
            const auto prev = mGL->GetIntAs<GLuint>(LOCAL_GL_VERTEX_ARRAY_BINDING);

            mGL->fGenVertexArrays(1, &mQuadVAO);
            mGL->fBindVertexArray(mQuadVAO);
            mGL->fEnableVertexAttribArray(0);
            mGL->fVertexAttribPointer(0, 2, LOCAL_GL_FLOAT, false, 0, 0);

            mGL->fBindVertexArray(prev);
        }
    }

    // --

    if (!mGL->IsGLES()) {
        const auto glslVersion = mGL->ShadingLanguageVersion();
        if (glslVersion >= 130) {
            mDrawBlitProg_VersionLine = nsPrintfCString("#version %u\n", glslVersion);
        }
    }

    const char kVertSource[] = "\
        #if __VERSION__ >= 130                                               \n\
            #define ATTRIBUTE in                                             \n\
            #define VARYING out                                              \n\
        #else                                                                \n\
            #define ATTRIBUTE attribute                                      \n\
            #define VARYING varying                                          \n\
        #endif                                                               \n\
                                                                             \n\
        ATTRIBUTE vec2 aVert;                                                \n\
                                                                             \n\
        uniform float u1ForYFlip;                                            \n\
        uniform vec4 uSrcRect;                                               \n\
        uniform vec2 uTexSize0;                                              \n\
        uniform vec2 uTexSize1;                                              \n\
        uniform vec2 uDivisors;                                              \n\
                                                                             \n\
        VARYING vec2 vTexCoord0;                                             \n\
        VARYING vec2 vTexCoord1;                                             \n\
                                                                             \n\
        void main(void)                                                      \n\
        {                                                                    \n\
            vec2 vertPos = aVert * 2.0 - 1.0;                                \n\
            gl_Position = vec4(vertPos, 0.0, 1.0);                           \n\
                                                                             \n\
            vec2 texCoord = aVert;                                           \n\
            texCoord.y = abs(u1ForYFlip - texCoord.y);                       \n\
            texCoord = texCoord * uSrcRect.zw + uSrcRect.xy;                 \n\
                                                                             \n\
            vTexCoord0 = texCoord / uTexSize0;                               \n\
            vTexCoord1 = texCoord / (uTexSize1 * uDivisors);                 \n\
        }                                                                    \n\
    ";

    const char* const parts[] = {
        mDrawBlitProg_VersionLine.get(),
        kVertSource
    };
    mGL->fShaderSource(mDrawBlitProg_VertShader, ArrayLength(parts), parts, nullptr);
    mGL->fCompileShader(mDrawBlitProg_VertShader);
}

GLBlitHelper::~GLBlitHelper()
{
    for (const auto& pair : mDrawBlitProgs) {
        const auto& ptr = pair.second;
        delete ptr;
    }
    mDrawBlitProgs.clear();

    if (!mGL->MakeCurrent())
        return;

    mGL->fDeleteShader(mDrawBlitProg_VertShader);
    mGL->fDeleteBuffers(1, &mQuadVBO);

    if (mQuadVAO) {
        mGL->fDeleteVertexArrays(1, &mQuadVAO);
    }
}

// --

const DrawBlitProg*
GLBlitHelper::GetDrawBlitProg(const DrawBlitProg::Key& key) const
{
    const auto& res = mDrawBlitProgs.insert({key, nullptr});
    auto& pair = *(res.first);
    const auto& didInsert = res.second;
    if (didInsert) {
        pair.second = CreateDrawBlitProg(pair.first);
    }
    return pair.second;
}


const DrawBlitProg*
GLBlitHelper::CreateDrawBlitProg(const DrawBlitProg::Key& key) const
{
    const char kFragHeader_Global[] = "\
        #ifdef GL_ES                                                         \n\
            #ifdef GL_FRAGMENT_PRECISION_HIGH                                \n\
                precision highp float;                                       \n\
            #else                                                            \n\
                precision mediump float;                                     \n\
            #endif                                                           \n\
        #endif                                                               \n\
                                                                             \n\
        #if __VERSION__ >= 130                                               \n\
            #define VARYING in                                               \n\
            #define FRAG_COLOR oFragColor                                    \n\
                                                                             \n\
            out vec4 FRAG_COLOR;                                             \n\
        #else                                                                \n\
            #define VARYING varying                                          \n\
            #define FRAG_COLOR gl_FragColor                                  \n\
        #endif                                                               \n\
    ";

    const ScopedShader fs(mGL, LOCAL_GL_FRAGMENT_SHADER);
    const char* const parts[] = {
        mDrawBlitProg_VersionLine.get(),
        key.fragHeader,
        kFragHeader_Global,
        key.fragBody
    };
    mGL->fShaderSource(fs, ArrayLength(parts), parts, nullptr);
    mGL->fCompileShader(fs);

    const auto prog = mGL->fCreateProgram();
    mGL->fAttachShader(prog, mDrawBlitProg_VertShader);
    mGL->fAttachShader(prog, fs);

    mGL->fBindAttribLocation(prog, 0, "aPosition");
    mGL->fLinkProgram(prog);

    GLenum status = 0;
    mGL->fGetProgramiv(prog, LOCAL_GL_LINK_STATUS, (GLint*)&status);
    if (status == LOCAL_GL_TRUE) {
        const SaveRestoreCurrentProgram oldProg(mGL);
        mGL->fUseProgram(prog);
        const char* samplerNames[] = {
            "uTex0",
            "uTex1",
            "uTex2"
        };
        for (int i = 0; i < 3; i++) {
            const auto loc = mGL->fGetUniformLocation(prog, samplerNames[i]);
            if (loc == -1)
                break;
            mGL->fUniform1i(loc, i);
        }

        return new DrawBlitProg(this, prog);
    }

    GLuint progLogLen = 0;
    mGL->fGetProgramiv(prog, LOCAL_GL_INFO_LOG_LENGTH, (GLint*)&progLogLen);
    const UniquePtr<char[]> progLog(new char[progLogLen+1]);
    mGL->fGetProgramInfoLog(prog, progLogLen, nullptr, progLog.get());
    progLog[progLogLen] = 0;

    const auto& vs = mDrawBlitProg_VertShader;
    GLuint vsLogLen = 0;
    mGL->fGetShaderiv(vs, LOCAL_GL_INFO_LOG_LENGTH, (GLint*)&vsLogLen);
    const UniquePtr<char[]> vsLog(new char[vsLogLen+1]);
    mGL->fGetShaderInfoLog(vs, vsLogLen, nullptr, vsLog.get());
    progLog[progLogLen] = 0;

    GLuint fsLogLen = 0;
    mGL->fGetShaderiv(fs, LOCAL_GL_INFO_LOG_LENGTH, (GLint*)&fsLogLen);
    const UniquePtr<char[]> fsLog(new char[fsLogLen+1]);
    mGL->fGetShaderInfoLog(fs, fsLogLen, nullptr, fsLog.get());
    progLog[progLogLen] = 0;

    gfxCriticalError() << "DrawBlitProg link failed:\n"
                       << "progLog: " << progLog.get() << "\n"
                       << "vsLog: " << vsLog.get() << "\n"
                       << "fsLog: " << fsLog.get() << "\n";
    return nullptr;
}

// -----------------------------------------------------------------------------

bool
GLBlitHelper::BlitImageToFramebuffer(layers::Image* srcImage,
                                     const gfx::IntSize& destSize,
                                     OriginPos destOrigin)
{
    switch (srcImage->GetFormat()) {
    case ImageFormat::PLANAR_YCBCR:
        return BlitImage(static_cast<PlanarYCbCrImage*>(srcImage), destSize, destOrigin);

#ifdef MOZ_WIDGET_ANDROID
    case ImageFormat::SURFACE_TEXTURE:
        return BlitImage(static_cast<layers::SurfaceTextureImage*>(srcImage), destSize,
                         destOrigin);

    case ImageFormat::EGLIMAGE:
        return BlitImage(static_cast<layers::EGLImageImage*>(srcImage), destSize,
                         destOrigin);
#endif
#ifdef XP_MACOSX
    case ImageFormat::MAC_IOSURFACE:
        return BlitImage(srcImage->AsMacIOSurfaceImage(), destSize, destOrigin);
#endif
#ifdef XP_WIN
    case ImageFormat::GPU_VIDEO:
        return BlitImage(static_cast<layers::GPUVideoImage*>(srcImage), destSize,
                         destOrigin);
    case ImageFormat::D3D11_YCBCR_IMAGE:
        return BlitImage((layers::D3D11YCbCrImage*)srcImage, destSize,
                         destOrigin);
    case ImageFormat::D3D9_RGB32_TEXTURE:
        return false; // todo
#endif
    default:
        gfxCriticalError() << "Unhandled srcImage->GetFormat(): "
                           << uint32_t(srcImage->GetFormat());
        return false;
    }
}

// -------------------------------------

#ifdef MOZ_WIDGET_ANDROID
bool
GLBlitHelper::BlitImage(layers::SurfaceTextureImage* srcImage, const gfx::IntSize&,
                        const OriginPos) const
{
    // FIXME
    const auto& srcOrigin = srcImage->GetOriginPos();
    (void)srcOrigin;
    gfxCriticalError() << "BlitImage(SurfaceTextureImage) not implemented.";
    return false;
}

bool
GLBlitHelper::BlitImage(layers::EGLImageImage* const srcImage,
                        const gfx::IntSize& destSize, const OriginPos destOrigin) const
{
    const EGLImage eglImage = srcImage->GetImage();
    const EGLSync eglSync = srcImage->GetSync();
    if (eglSync) {
        EGLint status = sEGLLibrary.fClientWaitSync(EGL_DISPLAY(), eglSync, 0, LOCAL_EGL_FOREVER);
        if (status != LOCAL_EGL_CONDITION_SATISFIED) {
            return false;
        }
    }

    GLuint tex = 0;
    mGL->fGenTextures(1, &tex);

    const ScopedSaveMultiTex saveTex(mGL, 1, LOCAL_GL_TEXTURE_2D);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, tex);
    mGL->TexParams_SetClampNoMips();
    mGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_2D, eglImage);

    const auto& srcOrigin = srcImage->GetOriginPos();
    const bool yFlip = destOrigin != srcOrigin;
    const gfx::IntRect srcRect(0, 0, 1, 1);
    const gfx::IntSize srcSize(1, 1);
    const DrawBlitProg::BaseArgs baseArgs = { destSize, yFlip, srcRect, srcSize };

    const auto& prog = GetDrawBlitProg({kFragHeader_Tex2D, kFragBody_RGBA});
    MOZ_RELEASE_ASSERT(prog);
    prog->Draw(baseArgs);

    mGL->fDeleteTextures(1, &tex);
    return true;
}
#endif

// -------------------------------------

bool
GuessDivisors(const gfx::IntSize& ySize, const gfx::IntSize& uvSize,
              gfx::IntSize* const out_divisors)
{
    const gfx::IntSize divisors((ySize.width  == uvSize.width ) ? 1 : 2,
                                (ySize.height == uvSize.height) ? 1 : 2);
    if (uvSize.width  * divisors.width != ySize.width ||
        uvSize.height * divisors.height != ySize.height)
    {
        return false;
    }
    *out_divisors = divisors;
    return true;
}

bool
GLBlitHelper::BlitImage(layers::PlanarYCbCrImage* const yuvImage,
                        const gfx::IntSize& destSize, const OriginPos destOrigin)
{
    const auto& prog = GetDrawBlitProg({kFragHeader_Tex2D, kFragBody_PlanarYUV});
    MOZ_RELEASE_ASSERT(prog);

    if (!mYuvUploads[0]) {
        mGL->fGenTextures(3, mYuvUploads);
        const ScopedBindTexture bindTex(mGL, mYuvUploads[0]);
        mGL->TexParams_SetClampNoMips();
        mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[1]);
        mGL->TexParams_SetClampNoMips();
        mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[2]);
        mGL->TexParams_SetClampNoMips();
    }

    // --

    const PlanarYCbCrData* const yuvData = yuvImage->GetData();

    if (yuvData->mYSkip || yuvData->mCbSkip || yuvData->mCrSkip ||
        yuvData->mYSize.width < 0 || yuvData->mYSize.height < 0 ||
        yuvData->mCbCrSize.width < 0 || yuvData->mCbCrSize.height < 0 ||
        yuvData->mYStride < 0 || yuvData->mCbCrStride < 0)
    {
        gfxCriticalError() << "Unusual PlanarYCbCrData: "
                           << yuvData->mYSkip << ","
                           << yuvData->mCbSkip << ","
                           << yuvData->mCrSkip << ", "
                           << yuvData->mYSize.width << ","
                           << yuvData->mYSize.height << ", "
                           << yuvData->mCbCrSize.width << ","
                           << yuvData->mCbCrSize.height << ", "
                           << yuvData->mYStride << ","
                           << yuvData->mCbCrStride;
        return false;
    }

    const gfx::IntSize yTexSize(yuvData->mYStride, yuvData->mYSize.height);
    const gfx::IntSize uvTexSize(yuvData->mCbCrStride, yuvData->mCbCrSize.height);
    gfx::IntSize divisors;
    if (!GuessDivisors(yTexSize, uvTexSize, &divisors)) {
        gfxCriticalError() << "GuessDivisors failed:"
                           << yTexSize.width << ","
                           << yTexSize.height << ", "
                           << uvTexSize.width << ","
                           << uvTexSize.height;
        return false;
    }

    // --

    // RED textures aren't valid in GLES2, and ALPHA textures are not valid in desktop GL Core Profiles.
    // So use R8 textures on GL3.0+ and GLES3.0+, but LUMINANCE/LUMINANCE/UNSIGNED_BYTE otherwise.
    GLenum internalFormat;
    GLenum unpackFormat;
    if (mGL->IsAtLeast(gl::ContextProfile::OpenGLCore, 300) ||
        mGL->IsAtLeast(gl::ContextProfile::OpenGLES, 300))
    {
        internalFormat = LOCAL_GL_R8;
        unpackFormat = LOCAL_GL_RED;
    } else {
        internalFormat = LOCAL_GL_LUMINANCE;
        unpackFormat = LOCAL_GL_LUMINANCE;
    }

    // --

    const ScopedSaveMultiTex saveTex(mGL, 3, LOCAL_GL_TEXTURE_2D);
    const ResetUnpackState reset(mGL);

    if (yTexSize != mYuvUploads_YSize ||
        uvTexSize != mYuvUploads_UVSize)
    {
        mYuvUploads_YSize = yTexSize;
        mYuvUploads_UVSize = uvTexSize;

        mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
        mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[0]);
        mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, internalFormat,
                         yTexSize.width, yTexSize.height, 0,
                         unpackFormat, LOCAL_GL_UNSIGNED_BYTE, nullptr);
        for (int i = 1; i < 3; i++) {
            mGL->fActiveTexture(LOCAL_GL_TEXTURE0 + i);
            mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[i]);
            mGL->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, internalFormat,
                             uvTexSize.width, uvTexSize.height, 0,
                             unpackFormat, LOCAL_GL_UNSIGNED_BYTE, nullptr);
        }
    }

    // --

    mGL->fActiveTexture(LOCAL_GL_TEXTURE0);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[0]);
    mGL->fTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0, 0,
                        yTexSize.width, yTexSize.height,
                        unpackFormat, LOCAL_GL_UNSIGNED_BYTE, yuvData->mYChannel);
    mGL->fActiveTexture(LOCAL_GL_TEXTURE1);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[1]);
    mGL->fTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0, 0,
                        uvTexSize.width, uvTexSize.height,
                        unpackFormat, LOCAL_GL_UNSIGNED_BYTE, yuvData->mCbChannel);
    mGL->fActiveTexture(LOCAL_GL_TEXTURE2);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mYuvUploads[2]);
    mGL->fTexSubImage2D(LOCAL_GL_TEXTURE_2D, 0, 0, 0,
                        uvTexSize.width, uvTexSize.height,
                        unpackFormat, LOCAL_GL_UNSIGNED_BYTE, yuvData->mCrChannel);

    // --

    const auto srcOrigin = OriginPos::BottomLeft;
    const bool yFlip = (destOrigin != srcOrigin);
    const auto& clipRect = yuvData->GetPictureRect();
    const auto& colorSpace = yuvData->mYUVColorSpace;

    const DrawBlitProg::BaseArgs baseArgs = { destSize, yFlip, clipRect, yTexSize };
    const DrawBlitProg::YUVArgs yuvArgs = { uvTexSize, divisors, colorSpace };

    prog->Draw(baseArgs, &yuvArgs);
    return true;
}

// -------------------------------------

#ifdef XP_MACOSX
bool
GLBlitHelper::BlitImage(layers::MacIOSurfaceImage* const srcImage,
                        const gfx::IntSize& destSize, const OriginPos destOrigin) const
{
    MacIOSurface* const iosurf = srcImage->GetSurface();
    if (mGL->GetContextType() != GLContextType::CGL) {
        MOZ_ASSERT(false);
        return false;
    }
    const auto glCGL = static_cast<GLContextCGL*>(mGL);
    const auto cglContext = glCGL->GetCGLContext();

    const auto& srcOrigin = OriginPos::BottomLeft;
    const bool yFlip = destOrigin != srcOrigin;
    const gfx::IntRect clipRect({0, 0}, srcImage->GetSize());
    const gfx::IntSize texRectNormFactor(1, 1);

    const DrawBlitProg::BaseArgs baseArgs = { destSize, yFlip, clipRect,
                                              texRectNormFactor };
    DrawBlitProg::YUVArgs yuvArgs = { texRectNormFactor, {2,2}, YUVColorSpace::BT601 };
    const DrawBlitProg::YUVArgs* pYuvArgs = nullptr;

    auto planes = iosurf->GetPlaneCount();
    if (!planes) {
        planes = 1; // Bad API. No cookie.
    }

    const GLenum texTarget = LOCAL_GL_TEXTURE_RECTANGLE;
    const char* const fragHeader = kFragHeader_Tex2DRect;

    const ScopedSaveMultiTex saveTex(mGL, planes, texTarget);
    const ScopedTexture tex0(mGL);
    const ScopedTexture tex1(mGL);
    const ScopedTexture tex2(mGL);
    const GLuint texs[3] = {
        tex0,
        tex1,
        tex2
    };

    const auto pixelFormat = iosurf->GetPixelFormat();
    const auto formatChars = (const char*)&pixelFormat;
    const char formatStr[] = {
        formatChars[3],
        formatChars[2],
        formatChars[1],
        formatChars[0],
        0
    };
    if (mGL->ShouldSpew()) {
        printf_stderr("iosurf format: %s (0x%08x)\n", formatStr, uint32_t(pixelFormat));
    }

    const char* fragBody;
    GLenum internalFormats[3] = {0, 0, 0};
    GLenum unpackFormats[3] = {0, 0, 0};
    GLenum unpackTypes[3] = { LOCAL_GL_UNSIGNED_BYTE,
                              LOCAL_GL_UNSIGNED_BYTE,
                              LOCAL_GL_UNSIGNED_BYTE };
    switch (planes) {
    case 1:
        fragBody = kFragBody_RGBA;
        internalFormats[0] = LOCAL_GL_RGBA;
        unpackFormats[0] = LOCAL_GL_RGBA;
        break;
    case 2:
        fragBody = kFragBody_NV12;
        if (mGL->Version() >= 300) {
            internalFormats[0] = LOCAL_GL_R8;
            unpackFormats[0] = LOCAL_GL_RED;
            internalFormats[1] = LOCAL_GL_RG8;
            unpackFormats[1] = LOCAL_GL_RG;
        } else {
            internalFormats[0] = LOCAL_GL_LUMINANCE;
            unpackFormats[0] = LOCAL_GL_LUMINANCE;
            internalFormats[1] = LOCAL_GL_LUMINANCE_ALPHA;
            unpackFormats[1] = LOCAL_GL_LUMINANCE_ALPHA;
        }
        pYuvArgs = &yuvArgs;
        break;
    case 3:
        fragBody = kFragBody_PlanarYUV;
        if (mGL->Version() >= 300) {
            internalFormats[0] = LOCAL_GL_R8;
            unpackFormats[0] = LOCAL_GL_RED;
        } else {
            internalFormats[0] = LOCAL_GL_LUMINANCE;
            unpackFormats[0] = LOCAL_GL_LUMINANCE;
        }
        internalFormats[1] = internalFormats[0];
        internalFormats[2] = internalFormats[0];
        unpackFormats[1] = unpackFormats[0];
        unpackFormats[2] = unpackFormats[0];
        pYuvArgs = &yuvArgs;
        break;
    default:
        gfxCriticalError() << "Unexpected plane count: " << planes;
        return false;
    }

    if (pixelFormat == '2vuy') {
        fragBody = kFragBody_CrYCb;
        // APPLE_rgb_422 adds RGB_RAW_422_APPLE for `internalFormat`, but only RGB seems
        // to work?
        internalFormats[0] = LOCAL_GL_RGB;
        unpackFormats[0] = LOCAL_GL_RGB_422_APPLE;
        unpackTypes[0] = LOCAL_GL_UNSIGNED_SHORT_8_8_APPLE;
        pYuvArgs = &yuvArgs;
    }

    for (uint32_t p = 0; p < planes; p++) {
        mGL->fActiveTexture(LOCAL_GL_TEXTURE0 + p);
        mGL->fBindTexture(texTarget, texs[p]);
        mGL->TexParams_SetClampNoMips(texTarget);

        const auto width = iosurf->GetDevicePixelWidth(p);
        const auto height = iosurf->GetDevicePixelHeight(p);
        auto err = iosurf->CGLTexImageIOSurface2D(cglContext, texTarget,
                                                  internalFormats[p], width, height,
                                                  unpackFormats[p], unpackTypes[p], p);
        if (err) {
            const nsPrintfCString errStr("CGLTexImageIOSurface2D(context, target, 0x%04x,"
                                         " %u, %u, 0x%04x, 0x%04x, iosurfPtr, %u) -> %i",
                                         internalFormats[p], uint32_t(width),
                                         uint32_t(height), unpackFormats[p],
                                         unpackTypes[p], p, err);
            gfxCriticalError() << errStr.get() << " (iosurf format: " << formatStr << ")";
            return false;
        }
    }

    const auto& prog = GetDrawBlitProg({fragHeader, fragBody});
    if (!prog)
        return false;

    prog->Draw(baseArgs, pYuvArgs);
    return true;
}
#endif

// -----------------------------------------------------------------------------

void
GLBlitHelper::DrawBlitTextureToFramebuffer(const GLuint srcTex,
                                           const gfx::IntSize& srcSize,
                                           const gfx::IntSize& destSize,
                                           const GLenum srcTarget) const
{
    const gfx::IntRect clipRect(0, 0, srcSize.width, srcSize.height);

    DrawBlitProg::Key key;
    gfx::IntSize texSizeDivisor;
    switch (srcTarget) {
    case LOCAL_GL_TEXTURE_2D:
        key = {kFragHeader_Tex2D, kFragBody_RGBA};
        texSizeDivisor = srcSize;
        break;
    case LOCAL_GL_TEXTURE_RECTANGLE_ARB:
        key = {kFragHeader_Tex2DRect, kFragBody_RGBA};
        texSizeDivisor = gfx::IntSize(1, 1);
        break;
    default:
        gfxCriticalError() << "Unexpected srcTarget: " << srcTarget;
        return;
    }
    const auto& prog = GetDrawBlitProg(key);
    MOZ_ASSERT(prog);

    const ScopedSaveMultiTex saveTex(mGL, 1, srcTarget);
    mGL->fBindTexture(srcTarget, srcTex);

    const bool yFlip = false;
    const DrawBlitProg::BaseArgs baseArgs = { destSize, yFlip, clipRect, texSizeDivisor };
    prog->Draw(baseArgs);
}

// -----------------------------------------------------------------------------

void
GLBlitHelper::BlitFramebuffer(const gfx::IntSize& srcSize,
                              const gfx::IntSize& destSize) const
{
    MOZ_ASSERT(mGL->IsSupported(GLFeature::framebuffer_blit));

    const ScopedGLState scissor(mGL, LOCAL_GL_SCISSOR_TEST, false);
    mGL->fBlitFramebuffer(0, 0,  srcSize.width,  srcSize.height,
                          0, 0, destSize.width, destSize.height,
                          LOCAL_GL_COLOR_BUFFER_BIT,
                          LOCAL_GL_NEAREST);
}

// --

void
GLBlitHelper::BlitFramebufferToFramebuffer(const GLuint srcFB, const GLuint destFB,
                                           const gfx::IntSize& srcSize,
                                           const gfx::IntSize& destSize) const
{
    MOZ_ASSERT(mGL->IsSupported(GLFeature::framebuffer_blit));
    MOZ_ASSERT(!srcFB || mGL->fIsFramebuffer(srcFB));
    MOZ_ASSERT(!destFB || mGL->fIsFramebuffer(destFB));

    const ScopedBindFramebuffer boundFB(mGL);
    mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, srcFB);
    mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destFB);

    BlitFramebuffer(srcSize, destSize);
}

void
GLBlitHelper::BlitTextureToFramebuffer(GLuint srcTex, const gfx::IntSize& srcSize,
                                       const gfx::IntSize& destSize,
                                       GLenum srcTarget) const
{
    MOZ_ASSERT(mGL->fIsTexture(srcTex));

    if (mGL->IsSupported(GLFeature::framebuffer_blit)) {
        const ScopedFramebufferForTexture srcWrapper(mGL, srcTex, srcTarget);
        const ScopedBindFramebuffer bindFB(mGL);
        mGL->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, srcWrapper.FB());
        BlitFramebuffer(srcSize, destSize);
        return;
    }

    DrawBlitTextureToFramebuffer(srcTex, srcSize, destSize, srcTarget);
}

void
GLBlitHelper::BlitFramebufferToTexture(GLuint destTex,
                                       const gfx::IntSize& srcSize,
                                       const gfx::IntSize& destSize,
                                       GLenum destTarget) const
{
    MOZ_ASSERT(mGL->fIsTexture(destTex));

    if (mGL->IsSupported(GLFeature::framebuffer_blit)) {
        const ScopedFramebufferForTexture destWrapper(mGL, destTex, destTarget);
        const ScopedBindFramebuffer bindFB(mGL);
        mGL->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, destWrapper.FB());
        BlitFramebuffer(srcSize, destSize);
        return;
    }

    ScopedBindTexture autoTex(mGL, destTex, destTarget);
    ScopedGLState scissor(mGL, LOCAL_GL_SCISSOR_TEST, false);
    mGL->fCopyTexSubImage2D(destTarget, 0,
                            0, 0,
                            0, 0,
                            srcSize.width, srcSize.height);
}

void
GLBlitHelper::BlitTextureToTexture(GLuint srcTex, GLuint destTex,
                                   const gfx::IntSize& srcSize,
                                   const gfx::IntSize& destSize,
                                   GLenum srcTarget, GLenum destTarget) const
{
    MOZ_ASSERT(mGL->fIsTexture(srcTex));
    MOZ_ASSERT(mGL->fIsTexture(destTex));

    // Start down the CopyTexSubImage path, not the DrawBlit path.
    const ScopedFramebufferForTexture srcWrapper(mGL, srcTex, srcTarget);
    const ScopedBindFramebuffer bindFB(mGL, srcWrapper.FB());
    BlitFramebufferToTexture(destTex, srcSize, destSize, destTarget);
}

} // namespace gl
} // namespace mozilla
