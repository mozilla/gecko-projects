/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrBicubicTextureEffect_DEFINED
#define GrBicubicTextureEffect_DEFINED

#include "GrSingleTextureEffect.h"
#include "GrTextureDomain.h"
#include "glsl/GrGLSLFragmentProcessor.h"

class GrGLBicubicEffect;
class GrInvariantOutput;

class GrBicubicEffect : public GrSingleTextureEffect {
public:
    enum {
        kFilterTexelPad = 2, // Given a src rect in texels to be filtered, this number of
                             // surrounding texels are needed by the kernel in x and y.
    };
    virtual ~GrBicubicEffect();

    const float* coefficients() const { return fCoefficients; }

    const char* name() const override { return "Bicubic"; }

    const GrTextureDomain& domain() const { return fDomain; }

    /**
     * Create a simple filter effect with custom bicubic coefficients and optional domain.
     */
    static const GrFragmentProcessor* Create(GrTexture* tex, const SkScalar coefficients[16],
                                             const SkRect* domain = nullptr) {
        if (nullptr == domain) {
            static const SkShader::TileMode kTileModes[] = { SkShader::kClamp_TileMode,
                                                             SkShader::kClamp_TileMode };
            return Create(tex, coefficients, GrCoordTransform::MakeDivByTextureWHMatrix(tex),
                          kTileModes);
        } else {
            return new GrBicubicEffect(tex, coefficients,
                                       GrCoordTransform::MakeDivByTextureWHMatrix(tex), *domain);
        }
    }

    /**
     * Create a Mitchell filter effect with specified texture matrix and x/y tile modes.
     */
    static const GrFragmentProcessor* Create(GrTexture* tex, const SkMatrix& matrix,
                                             const SkShader::TileMode tileModes[2]) {
        return Create(tex, gMitchellCoefficients, matrix, tileModes);
    }

    /**
     * Create a filter effect with custom bicubic coefficients, the texture matrix, and the x/y
     * tilemodes.
     */
    static const GrFragmentProcessor* Create(GrTexture* tex, const SkScalar coefficients[16],
                                             const SkMatrix& matrix,
                                             const SkShader::TileMode tileModes[2]) {
        return new GrBicubicEffect(tex, coefficients, matrix, tileModes);
    }

    /**
     * Create a Mitchell filter effect with a texture matrix and a domain.
     */
    static const GrFragmentProcessor* Create(GrTexture* tex, const SkMatrix& matrix,
                                             const SkRect& domain) {
        return new GrBicubicEffect(tex, gMitchellCoefficients, matrix, domain);
    }

    /**
     * Determines whether the bicubic effect should be used based on the transformation from the
     * local coords to the device. Returns true if the bicubic effect should be used. filterMode
     * is set to appropriate filtering mode to use regardless of the return result (e.g. when this
     * returns false it may indicate that the best fallback is to use kMipMap, kBilerp, or
     * kNearest).
     */
    static bool ShouldUseBicubic(const SkMatrix& localCoordsToDevice,
                                 GrTextureParams::FilterMode* filterMode);

private:
    GrBicubicEffect(GrTexture*, const SkScalar coefficients[16], const SkMatrix &matrix,
                    const SkShader::TileMode tileModes[2]);
    GrBicubicEffect(GrTexture*, const SkScalar coefficients[16], const SkMatrix &matrix,
                    const SkRect& domain);

    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;

    void onGetGLSLProcessorKey(const GrGLSLCaps&, GrProcessorKeyBuilder*) const override;

    bool onIsEqual(const GrFragmentProcessor&) const override;

    void onComputeInvariantOutput(GrInvariantOutput* inout) const override;

    float           fCoefficients[16];
    GrTextureDomain fDomain;

    GR_DECLARE_FRAGMENT_PROCESSOR_TEST;

    static const SkScalar gMitchellCoefficients[16];

    typedef GrSingleTextureEffect INHERITED;
};

#endif
