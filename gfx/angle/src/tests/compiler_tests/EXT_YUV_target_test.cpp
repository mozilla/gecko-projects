//
// Copyright (c) 2017 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// EXT_YUV_target.cpp:
//   Test for EXT_YUV_target implementation.
//

#include "angle_gl.h"
#include "gtest/gtest.h"
#include "GLSLANG/ShaderLang.h"

using testing::Combine;
using testing::Values;

namespace
{
const char ESSLVersion300[] = "#version 300 es\n";
const char EXTYTPragma[]    = "#extension GL_EXT_YUV_target : require\n";

const char ESSL300_SimpleShader[] =
    "precision mediump float;\n"
    "uniform __samplerExternal2DY2YEXT uSampler;\n"
    "out vec4 fragColor;\n"
    "void main() { \n"
    "    fragColor = vec4(1.0);\n"
    "}\n";

// Shader that samples the texture and writes to FragColor.
const char ESSL300_FragColorShader[] =
    "precision mediump float;\n"
    "uniform __samplerExternal2DY2YEXT uSampler;\n"
    "layout(yuv) out vec4 fragColor;\n"
    "void main() { \n"
    "    fragColor = texture(uSampler, vec2(0.0));\n"
    "}\n";

// Shader that specifies yuv layout qualifier multiple times.
const char ESSL300_YUVQualifierMultipleTimesShader[] =
    "precision mediump float;\n"
    "layout(yuv, yuv, yuv) out vec4 fragColor;\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuv layout qualifier for not output fails to compile.
const char ESSL300_YUVQualifierFailureShader1[] =
    "precision mediump float;\n"
    "layout(yuv) in vec4 fragColor;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YUVQualifierFailureShader2[] =
    "precision mediump float;\n"
    "layout(yuv) uniform;\n"
    "layout(yuv) uniform Transform {\n"
    "     mat4 M1;\n"
    "}\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuv layout qualifier with location fails to compile.
const char ESSL300_LocationAndYUVFailureShader[] =
    "precision mediump float;\n"
    "layout(location = 0, yuv) out vec4 fragColor;\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuv layout qualifier with multiple color outputs fails to compile.
const char ESSL300_MultipleColorAndYUVOutputsFailureShader1[] =
    "precision mediump float;\n"
    "layout(yuv) out vec4 fragColor;\n"
    "layout out vec4 fragColor1;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_MultipleColorAndYUVOutputsFailureShader2[] =
    "precision mediump float;\n"
    "layout(yuv) out vec4 fragColor;\n"
    "layout(location = 1) out vec4 fragColor1;\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuv layout qualifier with depth output fails to compile.
const char ESSL300_DepthAndYUVOutputsFailureShader[] =
    "precision mediump float;\n"
    "layout(yuv) out vec4 fragColor;\n"
    "void main() { \n"
    "    gl_FragDepth = 1.0f;\n"
    "}\n";

// Shader that specifies yuv layout qualifier with multiple outputs fails to compile.
const char ESSL300_MultipleYUVOutputsFailureShader[] =
    "precision mediump float;\n"
    "layout(yuv) out vec4 fragColor;\n"
    "layout(yuv) out vec4 fragColor1;\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuvCscStandartEXT type and associated values.
const char ESSL300_YuvCscStandardEXTShader[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT;\n"
    "yuvCscStandardEXT conv;\n"
    "yuvCscStandardEXT conv1 = itu_601;\n"
    "yuvCscStandardEXT conv2 = itu_601_full_range;\n"
    "yuvCscStandardEXT conv3 = itu_709;\n"
    "const yuvCscStandardEXT conv4 = itu_709;\n"
    "yuvCscStandardEXT conv_standard() {\n"
    "    return itu_601;\n"
    "}\n"
    "bool is_itu_601(inout yuvCscStandardEXT csc) {\n"
    "    csc = itu_601;\n"
    "    return csc == itu_601;\n"
    "}\n"
    "bool is_itu_709(yuvCscStandardEXT csc) {\n"
    "    return csc == itu_709;\n"
    "}\n"
    "void main() { \n"
    "    yuvCscStandardEXT conv = conv_standard();\n"
    "    bool csc_check1 = is_itu_601(conv);\n"
    "    bool csc_check2 = is_itu_709(itu_709);\n"
    "}\n";

// Shader that specifies yuvCscStandartEXT type constructor fails to compile.
const char ESSL300_YuvCscStandartdEXTConstructFailureShader1[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = yuvCscStandardEXT();\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTConstructFailureShader2[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = yuvCscStandardEXT(itu_601);\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuvCscStandartEXT type conversion fails to compile.
const char ESSL300_YuvCscStandartdEXTConversionFailureShader1[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = false;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTConversionFailureShader2[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = 0;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTConversionFailureShader3[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = 2.0f;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTConversionFailureShader4[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = itu_601 | itu_709;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTConversionFailureShader5[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = itu_601 & 3.0f;\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuvCscStandartEXT type qualifiers fails to compile.
const char ESSL300_YuvCscStandartdEXTQualifiersFailureShader1[] =
    "precision mediump float;\n"
    "in yuvCscStandardEXT conv = itu_601;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTQualifiersFailureShader2[] =
    "precision mediump float;\n"
    "out yuvCscStandardEXT conv = itu_601;\n"
    "void main() { \n"
    "}\n";

const char ESSL300_YuvCscStandartdEXTQualifiersFailureShader3[] =
    "precision mediump float;\n"
    "uniform yuvCscStandardEXT conv = itu_601;\n"
    "void main() { \n"
    "}\n";

// Shader that specifies yuv_to_rgb() and rgb_to_yuv() built-in functions.
const char ESSL300_BuiltInFunctionsShader[] =
    "precision mediump float;\n"
    "yuvCscStandardEXT conv = itu_601;\n"
    "void main() { \n"
    "    vec3 yuv = rgb_2_yuv(vec3(0.0f), conv);\n"
    "    vec3 rgb = yuv_2_rgb(yuv, itu_601);\n"
    "}\n";

class EXTYUVTargetTest : public testing::TestWithParam<testing::tuple<const char *, const char *>>
{
  protected:
    virtual void SetUp()
    {
        sh::InitBuiltInResources(&mResources);
        mResources.EXT_YUV_target = 1;

        mCompiler = nullptr;
    }

    virtual void TearDown() { DestroyCompiler(); }
    void DestroyCompiler()
    {
        if (mCompiler)
        {
            sh::Destruct(mCompiler);
            mCompiler = nullptr;
        }
    }

    void InitializeCompiler()
    {
        DestroyCompiler();
        mCompiler =
            sh::ConstructCompiler(GL_FRAGMENT_SHADER, SH_GLES3_SPEC, SH_ESSL_OUTPUT, &mResources);
        ASSERT_TRUE(mCompiler != nullptr) << "Compiler could not be constructed.";
    }

    testing::AssertionResult TestShaderCompile(const char *pragma)
    {
        return TestShaderCompile(testing::get<0>(GetParam()),  // Version.
                                 pragma,
                                 testing::get<1>(GetParam())  // Shader.
                                 );
    }

    testing::AssertionResult TestShaderCompile(const char *version,
                                               const char *pragma,
                                               const char *shader)
    {
        const char *shaderStrings[] = {version, pragma, shader};
        bool success                = sh::Compile(mCompiler, shaderStrings, 3, 0);
        if (success)
        {
            return ::testing::AssertionSuccess() << "Compilation success";
        }
        return ::testing::AssertionFailure() << sh::GetInfoLog(mCompiler);
    }

  protected:
    ShBuiltInResources mResources;
    ShHandle mCompiler;
};

// Extension flag is required to compile properly. Expect failure when it is
// not present.
TEST_P(EXTYUVTargetTest, CompileFailsWithoutExtension)
{
    mResources.EXT_YUV_target = 0;
    InitializeCompiler();
    EXPECT_FALSE(TestShaderCompile(EXTYTPragma));
}

// Extension directive is required to compile properly. Expect failure when
// it is not present.
TEST_P(EXTYUVTargetTest, CompileFailsWithExtensionWithoutPragma)
{
    mResources.EXT_YUV_target = 1;
    InitializeCompiler();
    EXPECT_FALSE(TestShaderCompile(""));
}

// With extension flag and extension directive, compiling succeeds.
// Also test that the extension directive state is reset correctly.
TEST_P(EXTYUVTargetTest, CompileSucceedsWithExtensionAndPragma)
{
    mResources.EXT_YUV_target = 1;
    InitializeCompiler();
    EXPECT_TRUE(TestShaderCompile(EXTYTPragma));
    // Test reset functionality.
    EXPECT_FALSE(TestShaderCompile(""));
    EXPECT_TRUE(TestShaderCompile(EXTYTPragma));
}

INSTANTIATE_TEST_CASE_P(CorrectVariantsWithExtensionAndPragma,
                        EXTYUVTargetTest,
                        Combine(Values(ESSLVersion300),
                                Values(ESSL300_SimpleShader, ESSL300_FragColorShader)));

class EXTYUVTargetCompileSuccessTest : public EXTYUVTargetTest
{
};

TEST_P(EXTYUVTargetCompileSuccessTest, CompileSucceeds)
{
    // Expect compile success.
    mResources.EXT_YUV_target = 1;
    InitializeCompiler();
    EXPECT_TRUE(TestShaderCompile(EXTYTPragma));
}

INSTANTIATE_TEST_CASE_P(CorrectESSL300Shaders,
                        EXTYUVTargetCompileSuccessTest,
                        Combine(Values(ESSLVersion300),
                                Values(ESSL300_FragColorShader,
                                       ESSL300_YUVQualifierMultipleTimesShader,
                                       ESSL300_YuvCscStandardEXTShader,
                                       ESSL300_BuiltInFunctionsShader)));

class EXTYUVTargetCompileFailureTest : public EXTYUVTargetTest
{
};

TEST_P(EXTYUVTargetCompileFailureTest, CompileFails)
{
    // Expect compile failure due to shader error, with shader having correct pragma.
    mResources.EXT_YUV_target = 1;
    InitializeCompiler();
    EXPECT_FALSE(TestShaderCompile(EXTYTPragma));
}

INSTANTIATE_TEST_CASE_P(IncorrectESSL300Shaders,
                        EXTYUVTargetCompileFailureTest,
                        Combine(Values(ESSLVersion300),
                                Values(ESSL300_YUVQualifierFailureShader1,
                                       ESSL300_YUVQualifierFailureShader2,
                                       ESSL300_LocationAndYUVFailureShader,
                                       ESSL300_MultipleColorAndYUVOutputsFailureShader1,
                                       ESSL300_MultipleColorAndYUVOutputsFailureShader2,
                                       ESSL300_DepthAndYUVOutputsFailureShader,
                                       ESSL300_MultipleYUVOutputsFailureShader,
                                       ESSL300_YuvCscStandartdEXTConstructFailureShader1,
                                       ESSL300_YuvCscStandartdEXTConstructFailureShader2,
                                       ESSL300_YuvCscStandartdEXTConversionFailureShader1,
                                       ESSL300_YuvCscStandartdEXTConversionFailureShader2,
                                       ESSL300_YuvCscStandartdEXTConversionFailureShader3,
                                       ESSL300_YuvCscStandartdEXTConversionFailureShader4,
                                       ESSL300_YuvCscStandartdEXTConversionFailureShader5,
                                       ESSL300_YuvCscStandartdEXTQualifiersFailureShader1,
                                       ESSL300_YuvCscStandartdEXTQualifiersFailureShader2,
                                       ESSL300_YuvCscStandartdEXTQualifiersFailureShader3)));

}  // namespace
