//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "test_utils/ANGLETest.h"
#include "test_utils/gl_raii.h"

using namespace angle;

namespace
{

class UniformBufferTest : public ANGLETest
{
  protected:
    UniformBufferTest()
    {
        setWindowWidth(128);
        setWindowHeight(128);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    void SetUp() override
    {
        ANGLETest::SetUp();

        mVertexShaderSource =
            R"(#version 300 es
            in vec4 position;
            void main()
            {
                gl_Position = position;
            })";

        mFragmentShaderSource =
            R"(#version 300 es
            precision highp float;
            uniform uni { vec4 color; };
            out vec4 fragColor;
            void main()
            {
                fragColor = color;
            })";

        mProgram = CompileProgram(mVertexShaderSource, mFragmentShaderSource);
        ASSERT_NE(mProgram, 0u);

        mUniformBufferIndex = glGetUniformBlockIndex(mProgram, "uni");
        ASSERT_NE(mUniformBufferIndex, -1);

        glGenBuffers(1, &mUniformBuffer);

        ASSERT_GL_NO_ERROR();
    }

    void TearDown() override
    {
        glDeleteBuffers(1, &mUniformBuffer);
        glDeleteProgram(mProgram);
        ANGLETest::TearDown();
    }

    std::string mVertexShaderSource;
    std::string mFragmentShaderSource;
    GLuint mProgram;
    GLint mUniformBufferIndex;
    GLuint mUniformBuffer;
};

// Basic UBO functionality.
TEST_P(UniformBufferTest, Simple)
{
    glClear(GL_COLOR_BUFFER_BIT);
    float floatData[4] = {0.5f, 0.75f, 0.25f, 1.0f};

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, floatData, GL_STATIC_DRAW);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);

    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);
    drawQuad(mProgram, "position", 0.5f);

    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_NEAR(0, 0, 128, 191, 64, 255, 1);
}

// Test that using a UBO with a non-zero offset and size actually works.
// The first step of this test renders a color from a UBO with a zero offset.
// The second step renders a color from a UBO with a non-zero offset.
TEST_P(UniformBufferTest, UniformBufferRange)
{
    int px = getWindowWidth() / 2;
    int py = getWindowHeight() / 2;

    // Query the uniform buffer alignment requirement
    GLint alignment;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);

    GLint64 maxUniformBlockSize;
    glGetInteger64v(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUniformBlockSize);
    if (alignment >= maxUniformBlockSize)
    {
        // ANGLE doesn't implement UBO offsets for this platform.
        // Ignore the test case.
        return;
    }

    ASSERT_GL_NO_ERROR();

    // Let's create a buffer which contains two vec4.
    GLuint vec4Size = 4 * sizeof(float);
    GLuint stride   = 0;
    do
    {
        stride += alignment;
    } while (stride < vec4Size);

    std::vector<char> v(2 * stride);
    float *first  = reinterpret_cast<float *>(v.data());
    float *second = reinterpret_cast<float *>(v.data() + stride);

    first[0] = 10.f / 255.f;
    first[1] = 20.f / 255.f;
    first[2] = 30.f / 255.f;
    first[3] = 40.f / 255.f;

    second[0] = 110.f / 255.f;
    second[1] = 120.f / 255.f;
    second[2] = 130.f / 255.f;
    second[3] = 140.f / 255.f;

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    // We use on purpose a size which is not a multiple of the alignment.
    glBufferData(GL_UNIFORM_BUFFER, stride + vec4Size, v.data(), GL_STATIC_DRAW);

    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);

    EXPECT_GL_NO_ERROR();

    // Bind the first part of the uniform buffer and draw
    // Use a size which is smaller than the alignment to check
    // to check that this case is handle correctly in the conversion to 11.1.
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, mUniformBuffer, 0, vec4Size);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 10, 20, 30, 40);

    // Bind the second part of the uniform buffer and draw
    // Furthermore the D3D11.1 backend will internally round the vec4Size (16 bytes) to a stride
    // (256 bytes) hence it will try to map the range [stride, 2 * stride] which is out-of-bound of
    // the buffer bufferSize = stride + vec4Size < 2 * stride. Ensure that this behaviour works.
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, mUniformBuffer, stride, vec4Size);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 110, 120, 130, 140);
}

// Test uniform block bindings.
TEST_P(UniformBufferTest, UniformBufferBindings)
{
    int px = getWindowWidth() / 2;
    int py = getWindowHeight() / 2;

    ASSERT_GL_NO_ERROR();

    // Let's create a buffer which contains one vec4.
    GLuint vec4Size = 4 * sizeof(float);
    std::vector<char> v(vec4Size);
    float *first = reinterpret_cast<float *>(v.data());

    first[0] = 10.f / 255.f;
    first[1] = 20.f / 255.f;
    first[2] = 30.f / 255.f;
    first[3] = 40.f / 255.f;

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, vec4Size, v.data(), GL_STATIC_DRAW);

    EXPECT_GL_NO_ERROR();

    // Try to bind the buffer to binding point 2
    glUniformBlockBinding(mProgram, mUniformBufferIndex, 2);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, mUniformBuffer);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 10, 20, 30, 40);

    // Clear the framebuffer
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(px, py, 0, 0, 0, 0);

    // Try to bind the buffer to another binding point
    glUniformBlockBinding(mProgram, mUniformBufferIndex, 5);
    glBindBufferBase(GL_UNIFORM_BUFFER, 5, mUniformBuffer);
    drawQuad(mProgram, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 10, 20, 30, 40);
}

// Test that ANGLE handles used but unbound UBO.
// TODO: A test case shouldn't depend on the error code of an undefined behaviour. Move this to unit
// tests of the validation layer.
TEST_P(UniformBufferTest, UnboundUniformBuffer)
{
    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
    EXPECT_GL_NO_ERROR();

    drawQuad(mProgram, "position", 0.5f);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);
}

// Update a UBO many time and verify that ANGLE uses the latest version of the data.
// https://code.google.com/p/angleproject/issues/detail?id=965
TEST_P(UniformBufferTest, UniformBufferManyUpdates)
{
    // TODO(jmadill): Figure out why this fails on Intel OpenGL.
    if (IsIntel() && IsOpenGL())
    {
        std::cout << "Test skipped on Intel OpenGL." << std::endl;
        return;
    }

    int px = getWindowWidth() / 2;
    int py = getWindowHeight() / 2;

    ASSERT_GL_NO_ERROR();

    float data[4];

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(data), nullptr, GL_DYNAMIC_DRAW);
    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);

    EXPECT_GL_NO_ERROR();

    // Repeteadly update the data and draw
    for (size_t i = 0; i < 10; ++i)
    {
        data[0] = (i + 10.f) / 255.f;
        data[1] = (i + 20.f) / 255.f;
        data[2] = (i + 30.f) / 255.f;
        data[3] = (i + 40.f) / 255.f;

        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), data);

        drawQuad(mProgram, "position", 0.5f);
        EXPECT_GL_NO_ERROR();
        EXPECT_PIXEL_EQ(px, py, i + 10, i + 20, i + 30, i + 40);
    }
}

// Use a large number of buffer ranges (compared to the actual size of the UBO)
TEST_P(UniformBufferTest, ManyUniformBufferRange)
{
    int px = getWindowWidth() / 2;
    int py = getWindowHeight() / 2;

    // Query the uniform buffer alignment requirement
    GLint alignment;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);

    GLint64 maxUniformBlockSize;
    glGetInteger64v(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUniformBlockSize);
    if (alignment >= maxUniformBlockSize)
    {
        // ANGLE doesn't implement UBO offsets for this platform.
        // Ignore the test case.
        return;
    }

    ASSERT_GL_NO_ERROR();

    // Let's create a buffer which contains eight vec4.
    GLuint vec4Size = 4 * sizeof(float);
    GLuint stride   = 0;
    do
    {
        stride += alignment;
    } while (stride < vec4Size);

    std::vector<char> v(8 * stride);

    for (size_t i = 0; i < 8; ++i)
    {
        float *data = reinterpret_cast<float *>(v.data() + i * stride);

        data[0] = (i + 10.f) / 255.f;
        data[1] = (i + 20.f) / 255.f;
        data[2] = (i + 30.f) / 255.f;
        data[3] = (i + 40.f) / 255.f;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, v.size(), v.data(), GL_STATIC_DRAW);

    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);

    EXPECT_GL_NO_ERROR();

    // Bind each possible offset
    for (size_t i = 0; i < 8; ++i)
    {
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, mUniformBuffer, i * stride, stride);
        drawQuad(mProgram, "position", 0.5f);
        EXPECT_GL_NO_ERROR();
        EXPECT_PIXEL_EQ(px, py, 10 + i, 20 + i, 30 + i, 40 + i);
    }

    // Try to bind larger range
    for (size_t i = 0; i < 7; ++i)
    {
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, mUniformBuffer, i * stride, 2 * stride);
        drawQuad(mProgram, "position", 0.5f);
        EXPECT_GL_NO_ERROR();
        EXPECT_PIXEL_EQ(px, py, 10 + i, 20 + i, 30 + i, 40 + i);
    }

    // Try to bind even larger range
    for (size_t i = 0; i < 5; ++i)
    {
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, mUniformBuffer, i * stride, 4 * stride);
        drawQuad(mProgram, "position", 0.5f);
        EXPECT_GL_NO_ERROR();
        EXPECT_PIXEL_EQ(px, py, 10 + i, 20 + i, 30 + i, 40 + i);
    }
}

// Tests that active uniforms have the right names.
TEST_P(UniformBufferTest, ActiveUniformNames)
{
    const std::string &vertexShaderSource =
        "#version 300 es\n"
        "in vec2 position;\n"
        "out vec2 v;\n"
        "uniform blockName1 {\n"
        "  float f1;\n"
        "} instanceName1;\n"
        "uniform blockName2 {\n"
        "  float f2;\n"
        "} instanceName2[1];\n"
        "void main() {\n"
        "  v = vec2(instanceName1.f1, instanceName2[0].f2);\n"
        "  gl_Position = vec4(position, 0, 1);\n"
        "}";

    const std::string &fragmentShaderSource =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 v;\n"
        "out vec4 color;\n"
        "void main() {\n"
        "  color = vec4(v, 0, 1);\n"
        "}";

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    ASSERT_NE(0u, program);

    GLint activeUniformBlocks;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &activeUniformBlocks);
    ASSERT_EQ(2, activeUniformBlocks);

    GLuint index = glGetUniformBlockIndex(program, "blockName1");
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();

    index = glGetUniformBlockIndex(program, "blockName2[0]");
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();

    GLint activeUniforms;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &activeUniforms);

    ASSERT_EQ(2, activeUniforms);

    GLint size;
    GLenum type;
    GLint maxLength;
    GLsizei length;

    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    std::vector<GLchar> strUniformNameBuffer(maxLength + 1, 0);
    const GLchar *uniformNames[1];
    uniformNames[0] = "blockName1.f1";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program, index, maxLength, &length, &size, &type, &strUniformNameBuffer[0]);
    EXPECT_EQ(1, size);
    EXPECT_GLENUM_EQ(GL_FLOAT, type);
    EXPECT_EQ("blockName1.f1", std::string(&strUniformNameBuffer[0]));

    uniformNames[0] = "blockName2.f2";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program, index, maxLength, &length, &size, &type, &strUniformNameBuffer[0]);
    EXPECT_EQ(1, size);
    EXPECT_GLENUM_EQ(GL_FLOAT, type);
    EXPECT_EQ("blockName2.f2", std::string(&strUniformNameBuffer[0]));
}

// Tests active uniforms and blocks when the layout is std140, shared and packed.
TEST_P(UniformBufferTest, ActiveUniformNumberAndName)
{
    const std::string &vertexShaderSource =
        "#version 300 es\n"
        "in vec2 position;\n"
        "out float v;\n"
        "struct S {\n"
        "  highp ivec3 a;\n"
        "  mediump ivec2 b[4];\n"
        "};\n"
        "layout(std140) uniform blockName0 {\n"
        "  S s0;\n"
        "  lowp vec2 v0;\n"
        "  S s1[2];\n"
        "  highp uint u0;\n"
        "};\n"
        "layout(std140) uniform blockName1 {\n"
        "  float f1;\n"
        "  bool b1;\n"
        "} instanceName1;\n"
        "layout(shared) uniform blockName2 {\n"
        "  float f2;\n"
        "};\n"
        "layout(packed) uniform blockName3 {\n"
        "  float f3;\n"
        "};\n"
        "void main() {\n"
        "  v = instanceName1.f1;\n"
        "  gl_Position = vec4(position, 0, 1);\n"
        "}";

    const std::string &fragmentShaderSource =
        "#version 300 es\n"
        "precision highp float;\n"
        "in float v;\n"
        "out vec4 color;\n"
        "void main() {\n"
        "  color = vec4(v, 0, 0, 1);\n"
        "}";

    ANGLE_GL_PROGRAM(program, vertexShaderSource, fragmentShaderSource);

    // Note that the packed |blockName3| might (or might not) be optimized out.
    GLint activeUniforms;
    glGetProgramiv(program.get(), GL_ACTIVE_UNIFORMS, &activeUniforms);
    EXPECT_GE(activeUniforms, 11);

    GLint activeUniformBlocks;
    glGetProgramiv(program.get(), GL_ACTIVE_UNIFORM_BLOCKS, &activeUniformBlocks);
    EXPECT_GE(activeUniformBlocks, 3);

    GLint maxLength, size;
    GLenum type;
    GLsizei length;
    GLuint index;
    const GLchar *uniformNames[1];
    glGetProgramiv(program.get(), GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    std::vector<GLchar> strBuffer(maxLength + 1, 0);

    uniformNames[0] = "s0.a";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    EXPECT_EQ(1, size);
    EXPECT_EQ("s0.a", std::string(&strBuffer[0]));

    uniformNames[0] = "s0.b[0]";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(4, size);
    EXPECT_EQ("s0.b[0]", std::string(&strBuffer[0]));

    uniformNames[0] = "v0";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("v0", std::string(&strBuffer[0]));

    uniformNames[0] = "s1[0].a";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("s1[0].a", std::string(&strBuffer[0]));

    uniformNames[0] = "s1[0].b[0]";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(4, size);
    EXPECT_EQ("s1[0].b[0]", std::string(&strBuffer[0]));

    uniformNames[0] = "s1[1].a";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("s1[1].a", std::string(&strBuffer[0]));

    uniformNames[0] = "s1[1].b[0]";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(4, size);
    EXPECT_EQ("s1[1].b[0]", std::string(&strBuffer[0]));

    uniformNames[0] = "u0";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("u0", std::string(&strBuffer[0]));

    uniformNames[0] = "blockName1.f1";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("blockName1.f1", std::string(&strBuffer[0]));

    uniformNames[0] = "blockName1.b1";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("blockName1.b1", std::string(&strBuffer[0]));

    uniformNames[0] = "f2";
    glGetUniformIndices(program, 1, uniformNames, &index);
    EXPECT_NE(GL_INVALID_INDEX, index);
    ASSERT_GL_NO_ERROR();
    glGetActiveUniform(program.get(), index, maxLength, &length, &size, &type, &strBuffer[0]);
    ASSERT_GL_NO_ERROR();
    EXPECT_EQ(1, size);
    EXPECT_EQ("f2", std::string(&strBuffer[0]));
}

// Test that using a very large buffer to back a small uniform block works OK.
TEST_P(UniformBufferTest, VeryLarge)
{
    glClear(GL_COLOR_BUFFER_BIT);
    float floatData[4] = {0.5f, 0.75f, 0.25f, 1.0f};

    GLsizei bigSize = 4096 * 64;
    std::vector<GLubyte> zero(bigSize, 0);

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, bigSize, zero.data(), GL_STATIC_DRAW);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(float) * 4, floatData);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);

    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);
    drawQuad(mProgram, "position", 0.5f);

    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_NEAR(0, 0, 128, 191, 64, 255, 1);
}

// Test that readback from a very large uniform buffer works OK.
TEST_P(UniformBufferTest, VeryLargeReadback)
{
    glClear(GL_COLOR_BUFFER_BIT);

    // Generate some random data.
    GLsizei bigSize = 4096 * 64;
    std::vector<GLubyte> expectedData(bigSize);
    for (GLsizei index = 0; index < bigSize; ++index)
    {
        expectedData[index] = static_cast<GLubyte>(index);
    }

    // Initialize the GL buffer.
    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, bigSize, expectedData.data(), GL_STATIC_DRAW);

    // Do a small update.
    GLsizei smallSize = sizeof(float) * 4;
    std::array<float, 4> floatData = {{0.5f, 0.75f, 0.25f, 1.0f}};
    memcpy(expectedData.data(), floatData.data(), smallSize);

    glBufferSubData(GL_UNIFORM_BUFFER, 0, smallSize, expectedData.data());

    // Draw with the buffer.
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);
    glUniformBlockBinding(mProgram, mUniformBufferIndex, 0);
    drawQuad(mProgram, "position", 0.5f);

    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_NEAR(0, 0, 128, 191, 64, 255, 1);

    // Read back the large buffer data.
    const void *mapPtr = glMapBufferRange(GL_UNIFORM_BUFFER, 0, bigSize, GL_MAP_READ_BIT);
    ASSERT_GL_NO_ERROR();
    const GLubyte *bytePtr = reinterpret_cast<const GLubyte *>(mapPtr);
    std::vector<GLubyte> actualData(bytePtr, bytePtr + bigSize);
    EXPECT_EQ(expectedData, actualData);

    glUnmapBuffer(GL_UNIFORM_BUFFER);
}

class UniformBufferTest31 : public ANGLETest
{
  protected:
    UniformBufferTest31()
    {
        setWindowWidth(128);
        setWindowHeight(128);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }
};

// Test uniform block bindings greater than GL_MAX_UNIFORM_BUFFER_BINDINGS cause compile error.
TEST_P(UniformBufferTest31, MaxUniformBufferBindingsExceeded)
{
    GLint maxUniformBufferBindings;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUniformBufferBindings);
    std::string source =
        "#version 310 es\n"
        "in vec4 position;\n"
        "layout(binding = ";
    std::stringstream ss;
    ss << maxUniformBufferBindings;
    source = source + ss.str() +
             ") uniform uni {\n"
             "    vec4 color;\n"
             "};\n"
             "void main()\n"
             "{\n"
             "    gl_Position = position;\n"
             "}";
    GLuint shader = CompileShader(GL_VERTEX_SHADER, source);
    EXPECT_EQ(0u, shader);
}

// Test uniform block bindings specified by layout in shader work properly.
TEST_P(UniformBufferTest31, UniformBufferBindings)
{
    const std::string &vertexShaderSource =
        "#version 310 es\n"
        "in vec4 position;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = position;\n"
        "}";
    const std::string &fragmentShaderSource =
        "#version 310 es\n"
        "precision highp float;\n"
        "layout(binding = 2) uniform uni {\n"
        "    vec4 color;\n"
        "};\n"
        "out vec4 fragColor;\n"
        "void main()\n"
        "{"
        "    fragColor = color;\n"
        "}";

    ANGLE_GL_PROGRAM(program, vertexShaderSource, fragmentShaderSource);
    GLuint uniformBufferIndex = glGetUniformBlockIndex(program, "uni");
    ASSERT_NE(GL_INVALID_INDEX, uniformBufferIndex);
    GLBuffer uniformBuffer;

    int px = getWindowWidth() / 2;
    int py = getWindowHeight() / 2;

    ASSERT_GL_NO_ERROR();

    // Let's create a buffer which contains one vec4.
    GLuint vec4Size = 4 * sizeof(float);
    std::vector<char> v(vec4Size);
    float *first = reinterpret_cast<float *>(v.data());

    first[0] = 10.f / 255.f;
    first[1] = 20.f / 255.f;
    first[2] = 30.f / 255.f;
    first[3] = 40.f / 255.f;

    glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer.get());
    glBufferData(GL_UNIFORM_BUFFER, vec4Size, v.data(), GL_STATIC_DRAW);

    EXPECT_GL_NO_ERROR();

    glBindBufferBase(GL_UNIFORM_BUFFER, 2, uniformBuffer.get());
    drawQuad(program, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 10, 20, 30, 40);

    // Clear the framebuffer
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_PIXEL_EQ(px, py, 0, 0, 0, 0);

    // Try to bind the buffer to another binding point
    glUniformBlockBinding(program, uniformBufferIndex, 5);
    glBindBufferBase(GL_UNIFORM_BUFFER, 5, uniformBuffer.get());
    drawQuad(program, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 10, 20, 30, 40);
}

// Test uniform blocks used as instanced array take next binding point for each subsequent element.
TEST_P(UniformBufferTest31, ConsecutiveBindingsForBlockArray)
{
    const std::string &vertexShaderSource =
        "#version 310 es\n"
        "in vec4 position;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = position;\n"
        "}";
    const std::string &fragmentShaderSource =
        "#version 310 es\n"
        "precision highp float;\n"
        "layout(binding = 2) uniform uni {\n"
        "    vec4 color;\n"
        "} blocks[2];\n"
        "out vec4 fragColor;\n"
        "void main()\n"
        "{\n"
        "    fragColor = blocks[0].color + blocks[1].color;\n"
        "}";

    ANGLE_GL_PROGRAM(program, vertexShaderSource, fragmentShaderSource);
    std::array<GLBuffer, 2> uniformBuffers;

    int px = getWindowWidth() / 2;
    int py = getWindowHeight() / 2;

    ASSERT_GL_NO_ERROR();

    // Let's create a buffer which contains one vec4.
    GLuint vec4Size = 4 * sizeof(float);
    std::vector<char> v(vec4Size);
    float *first = reinterpret_cast<float *>(v.data());

    first[0] = 10.f / 255.f;
    first[1] = 20.f / 255.f;
    first[2] = 30.f / 255.f;
    first[3] = 40.f / 255.f;

    glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffers[0].get());
    glBufferData(GL_UNIFORM_BUFFER, vec4Size, v.data(), GL_STATIC_DRAW);
    EXPECT_GL_NO_ERROR();
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, uniformBuffers[0].get());
    ASSERT_GL_NO_ERROR();
    glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffers[1].get());
    glBufferData(GL_UNIFORM_BUFFER, vec4Size, v.data(), GL_STATIC_DRAW);
    EXPECT_GL_NO_ERROR();
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, uniformBuffers[1].get());

    drawQuad(program, "position", 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_EQ(px, py, 20, 40, 60, 80);
}

// Test the layout qualifier binding must be both specified(ESSL 3.10.4 section 9.2).
TEST_P(UniformBufferTest31, BindingMustBeBothSpecified)
{
    const std::string &vertexShaderSource =
        "#version 310 es\n"
        "in vec4 position;\n"
        "uniform uni\n"
        "{\n"
        "    vec4 color;\n"
        "} block;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = position + block.color;\n"
        "}";
    const std::string &fragmentShaderSource =
        "#version 310 es\n"
        "precision highp float;\n"
        "layout(binding = 0) uniform uni\n"
        "{\n"
        "    vec4 color;\n"
        "} block;\n"
        "out vec4 fragColor;\n"
        "void main()\n"
        "{\n"
        "    fragColor = block.color;\n"
        "}";
    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    ASSERT_EQ(0u, program);
}

// Test with a block containing an array of structs.
TEST_P(UniformBufferTest, BlockContainingArrayOfStructs)
{
    const std::string &fragmentShader =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec4 my_FragColor;\n"
        "struct light_t {\n"
        "    vec4 intensity;\n"
        "};\n"
        "const int maxLights = 2;\n"
        "layout(std140) uniform lightData { light_t lights[maxLights]; };\n"
        "vec4 processLight(vec4 lighting, light_t light)\n"
        "{\n"
        "    return lighting + light.intensity;\n"
        "}\n"
        "void main()\n"
        "{\n"
        "    vec4 lighting = vec4(0, 0, 0, 1);\n"
        "    for (int n = 0; n < maxLights; n++)\n"
        "    {\n"
        "        lighting = processLight(lighting, lights[n]);\n"
        "    }\n"
        "    my_FragColor = lighting;\n"
        "}\n";

    ANGLE_GL_PROGRAM(program, mVertexShaderSource, fragmentShader);
    GLint uniformBufferIndex = glGetUniformBlockIndex(program, "lightData");

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    const GLsizei kStructCount        = 2;
    const GLsizei kVectorElementCount = 4;
    const GLsizei kBytesPerElement    = 4;
    const GLsizei kDataSize           = kStructCount * kVectorElementCount * kBytesPerElement;
    std::vector<GLubyte> v(kDataSize, 0);
    float *vAsFloat = reinterpret_cast<float *>(v.data());

    vAsFloat[1]                       = 0.5f;
    vAsFloat[kVectorElementCount + 1] = 0.5f;

    glBufferData(GL_UNIFORM_BUFFER, kDataSize, v.data(), GL_STATIC_DRAW);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);
    glUniformBlockBinding(program, uniformBufferIndex, 0);
    drawQuad(program.get(), "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Test with a block containing an array of structs containing arrays.
TEST_P(UniformBufferTest, BlockContainingArrayOfStructsContainingArrays)
{
    const std::string &fragmentShader =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec4 my_FragColor;\n"
        "struct light_t {\n"
        "    vec4 intensity[3];\n"
        "};\n"
        "const int maxLights = 2;\n"
        "layout(std140) uniform lightData { light_t lights[maxLights]; };\n"
        "vec4 processLight(vec4 lighting, light_t light)\n"
        "{\n"
        "    return lighting + light.intensity[1];\n"
        "}\n"
        "void main()\n"
        "{\n"
        "    vec4 lighting = vec4(0, 0, 0, 1);\n"
        "    for (int n = 0; n < maxLights; n++)\n"
        "    {\n"
        "        lighting = processLight(lighting, lights[n]);\n"
        "    }\n"
        "    my_FragColor = lighting;\n"
        "}\n";

    ANGLE_GL_PROGRAM(program, mVertexShaderSource, fragmentShader);
    GLint uniformBufferIndex = glGetUniformBlockIndex(program, "lightData");

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    const GLsizei kStructCount       = 2;
    const GLsizei kVectorsPerStruct  = 3;
    const GLsizei kElementsPerVector = 4;
    const GLsizei kBytesPerElement   = 4;
    const GLsizei kDataSize =
        kStructCount * kVectorsPerStruct * kElementsPerVector * kBytesPerElement;
    std::vector<GLubyte> v(kDataSize, 0);
    float *vAsFloat = reinterpret_cast<float *>(v.data());

    vAsFloat[kElementsPerVector + 1]                                          = 0.5f;
    vAsFloat[kVectorsPerStruct * kElementsPerVector + kElementsPerVector + 1] = 0.5f;

    glBufferData(GL_UNIFORM_BUFFER, kDataSize, v.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);
    glUniformBlockBinding(program, uniformBufferIndex, 0);
    drawQuad(program.get(), "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Test with a block containing nested structs.
TEST_P(UniformBufferTest, BlockContainingNestedStructs)
{
    const std::string &fragmentShader =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec4 my_FragColor;\n"
        "struct light_t {\n"
        "    vec4 intensity;\n"
        "};\n"
        "struct lightWrapper_t {\n"
        "    light_t light;\n"
        "};\n"
        "const int maxLights = 2;\n"
        "layout(std140) uniform lightData { lightWrapper_t lightWrapper; };\n"
        "vec4 processLight(vec4 lighting, lightWrapper_t aLightWrapper)\n"
        "{\n"
        "    return lighting + aLightWrapper.light.intensity;\n"
        "}\n"
        "void main()\n"
        "{\n"
        "    vec4 lighting = vec4(0, 0, 0, 1);\n"
        "    for (int n = 0; n < maxLights; n++)\n"
        "    {\n"
        "        lighting = processLight(lighting, lightWrapper);\n"
        "    }\n"
        "    my_FragColor = lighting;\n"
        "}\n";

    ANGLE_GL_PROGRAM(program, mVertexShaderSource, fragmentShader);
    GLint uniformBufferIndex = glGetUniformBlockIndex(program, "lightData");

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    const GLsizei kVectorsPerStruct  = 3;
    const GLsizei kElementsPerVector = 4;
    const GLsizei kBytesPerElement   = 4;
    const GLsizei kDataSize          = kVectorsPerStruct * kElementsPerVector * kBytesPerElement;
    std::vector<GLubyte> v(kDataSize, 0);
    float *vAsFloat = reinterpret_cast<float *>(v.data());

    vAsFloat[1] = 1.0f;

    glBufferData(GL_UNIFORM_BUFFER, kDataSize, v.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);
    glUniformBlockBinding(program, uniformBufferIndex, 0);
    drawQuad(program.get(), "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Tests GetUniformBlockIndex return value on error.
TEST_P(UniformBufferTest, GetUniformBlockIndexDefaultReturn)
{
    ASSERT_FALSE(glIsProgram(99));
    EXPECT_EQ(GL_INVALID_INDEX, glGetUniformBlockIndex(99, "farts"));
    EXPECT_GL_ERROR(GL_INVALID_VALUE);
}

// Block names can be reserved names in GLSL, as long as they're not reserved in GLSL ES.
TEST_P(UniformBufferTest, UniformBlockReservedOpenGLName)
{
    const std::string &fragmentShader =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec4 my_FragColor;\n"
        "layout(std140) uniform buffer { vec4 color; };\n"
        "void main()\n"
        "{\n"
        "    my_FragColor = color;\n"
        "}\n";

    ANGLE_GL_PROGRAM(program, mVertexShaderSource, fragmentShader);
    GLint uniformBufferIndex = glGetUniformBlockIndex(program, "buffer");

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    const GLsizei kElementsPerVector = 4;
    const GLsizei kBytesPerElement   = 4;
    const GLsizei kDataSize          = kElementsPerVector * kBytesPerElement;
    std::vector<GLubyte> v(kDataSize, 0);
    float *vAsFloat = reinterpret_cast<float *>(v.data());

    vAsFloat[1] = 1.0f;
    vAsFloat[3] = 1.0f;

    glBufferData(GL_UNIFORM_BUFFER, kDataSize, v.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);
    glUniformBlockBinding(program, uniformBufferIndex, 0);
    drawQuad(program.get(), "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Block instance names can be reserved names in GLSL, as long as they're not reserved in GLSL ES.
TEST_P(UniformBufferTest, UniformBlockInstanceReservedOpenGLName)
{
    const std::string &fragmentShader =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec4 my_FragColor;\n"
        "layout(std140) uniform dmat2 { vec4 color; } buffer;\n"
        "void main()\n"
        "{\n"
        "    my_FragColor = buffer.color;\n"
        "}\n";

    ANGLE_GL_PROGRAM(program, mVertexShaderSource, fragmentShader);
    GLint uniformBufferIndex = glGetUniformBlockIndex(program, "dmat2");

    glBindBuffer(GL_UNIFORM_BUFFER, mUniformBuffer);
    const GLsizei kElementsPerVector = 4;
    const GLsizei kBytesPerElement   = 4;
    const GLsizei kDataSize          = kElementsPerVector * kBytesPerElement;
    std::vector<GLubyte> v(kDataSize, 0);
    float *vAsFloat = reinterpret_cast<float *>(v.data());

    vAsFloat[1] = 1.0f;
    vAsFloat[3] = 1.0f;

    glBufferData(GL_UNIFORM_BUFFER, kDataSize, v.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, mUniformBuffer);
    glUniformBlockBinding(program, uniformBufferIndex, 0);
    drawQuad(program.get(), "position", 0.5f);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Use this to select which configurations (e.g. which renderer, which GLES major version) these tests should be run against.
ANGLE_INSTANTIATE_TEST(UniformBufferTest,
                       ES3_D3D11(),
                       ES3_D3D11_FL11_1(),
                       ES3_D3D11_FL11_1_REFERENCE(),
                       ES3_OPENGL(),
                       ES3_OPENGLES());
ANGLE_INSTANTIATE_TEST(UniformBufferTest31, ES31_D3D11(), ES31_OPENGL(), ES31_OPENGLES());

} // namespace
