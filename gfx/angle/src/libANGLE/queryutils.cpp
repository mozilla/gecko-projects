//
// Copyright (c) 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// queryutils.cpp: Utilities for querying values from GL objects

#include "libANGLE/queryutils.h"

#include "common/utilities.h"

#include "libANGLE/Buffer.h"
#include "libANGLE/Config.h"
#include "libANGLE/Context.h"
#include "libANGLE/Fence.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/Program.h"
#include "libANGLE/Renderbuffer.h"
#include "libANGLE/Sampler.h"
#include "libANGLE/Shader.h"
#include "libANGLE/Surface.h"
#include "libANGLE/Texture.h"
#include "libANGLE/Uniform.h"
#include "libANGLE/VertexAttribute.h"

namespace gl
{

namespace
{
template <typename ParamType>
void QueryTexLevelParameterBase(const Texture *texture,
                                GLenum target,
                                GLint level,
                                GLenum pname,
                                ParamType *params)
{
    ASSERT(texture != nullptr);
    const InternalFormat *info = texture->getTextureState().getImageDesc(target, level).format.info;

    switch (pname)
    {
        case GL_TEXTURE_RED_TYPE:
            *params = ConvertFromGLenum<ParamType>(info->redBits ? info->componentType : GL_NONE);
            break;
        case GL_TEXTURE_GREEN_TYPE:
            *params = ConvertFromGLenum<ParamType>(info->greenBits ? info->componentType : GL_NONE);
            break;
        case GL_TEXTURE_BLUE_TYPE:
            *params = ConvertFromGLenum<ParamType>(info->blueBits ? info->componentType : GL_NONE);
            break;
        case GL_TEXTURE_ALPHA_TYPE:
            *params = ConvertFromGLenum<ParamType>(info->alphaBits ? info->componentType : GL_NONE);
            break;
        case GL_TEXTURE_DEPTH_TYPE:
            *params = ConvertFromGLenum<ParamType>(info->depthBits ? info->componentType : GL_NONE);
            break;
        case GL_TEXTURE_RED_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->redBits);
            break;
        case GL_TEXTURE_GREEN_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->greenBits);
            break;
        case GL_TEXTURE_BLUE_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->blueBits);
            break;
        case GL_TEXTURE_ALPHA_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->alphaBits);
            break;
        case GL_TEXTURE_DEPTH_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->depthBits);
            break;
        case GL_TEXTURE_STENCIL_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->stencilBits);
            break;
        case GL_TEXTURE_SHARED_SIZE:
            *params = ConvertFromGLuint<ParamType>(info->sharedBits);
            break;
        case GL_TEXTURE_INTERNAL_FORMAT:
            *params =
                ConvertFromGLenum<ParamType>(info->internalFormat ? info->internalFormat : GL_RGBA);
            break;
        case GL_TEXTURE_WIDTH:
            *params =
                ConvertFromGLint<ParamType>(static_cast<GLint>(texture->getWidth(target, level)));
            break;
        case GL_TEXTURE_HEIGHT:
            *params =
                ConvertFromGLint<ParamType>(static_cast<GLint>(texture->getHeight(target, level)));
            break;
        case GL_TEXTURE_DEPTH:
            *params =
                ConvertFromGLint<ParamType>(static_cast<GLint>(texture->getDepth(target, level)));
            break;
        case GL_TEXTURE_SAMPLES:
            *params = ConvertFromGLint<ParamType>(texture->getSamples(target, level));
            break;
        case GL_TEXTURE_FIXED_SAMPLE_LOCATIONS:
            *params =
                ConvertFromGLboolean<ParamType>(texture->getFixedSampleLocations(target, level));
            break;
        case GL_TEXTURE_COMPRESSED:
            *params = ConvertFromGLboolean<ParamType>(info->compressed);
            break;
        default:
            UNREACHABLE();
            break;
    }
}

template <typename ParamType>
void QueryTexParameterBase(const Texture *texture, GLenum pname, ParamType *params)
{
    ASSERT(texture != nullptr);

    switch (pname)
    {
        case GL_TEXTURE_MAG_FILTER:
            *params = ConvertFromGLenum<ParamType>(texture->getMagFilter());
            break;
        case GL_TEXTURE_MIN_FILTER:
            *params = ConvertFromGLenum<ParamType>(texture->getMinFilter());
            break;
        case GL_TEXTURE_WRAP_S:
            *params = ConvertFromGLenum<ParamType>(texture->getWrapS());
            break;
        case GL_TEXTURE_WRAP_T:
            *params = ConvertFromGLenum<ParamType>(texture->getWrapT());
            break;
        case GL_TEXTURE_WRAP_R:
            *params = ConvertFromGLenum<ParamType>(texture->getWrapR());
            break;
        case GL_TEXTURE_IMMUTABLE_FORMAT:
            *params = ConvertFromGLboolean<ParamType>(texture->getImmutableFormat());
            break;
        case GL_TEXTURE_IMMUTABLE_LEVELS:
            *params = ConvertFromGLuint<ParamType>(texture->getImmutableLevels());
            break;
        case GL_TEXTURE_USAGE_ANGLE:
            *params = ConvertFromGLenum<ParamType>(texture->getUsage());
            break;
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
            *params = ConvertFromGLfloat<ParamType>(texture->getMaxAnisotropy());
            break;
        case GL_TEXTURE_SWIZZLE_R:
            *params = ConvertFromGLenum<ParamType>(texture->getSwizzleRed());
            break;
        case GL_TEXTURE_SWIZZLE_G:
            *params = ConvertFromGLenum<ParamType>(texture->getSwizzleGreen());
            break;
        case GL_TEXTURE_SWIZZLE_B:
            *params = ConvertFromGLenum<ParamType>(texture->getSwizzleBlue());
            break;
        case GL_TEXTURE_SWIZZLE_A:
            *params = ConvertFromGLenum<ParamType>(texture->getSwizzleAlpha());
            break;
        case GL_TEXTURE_BASE_LEVEL:
            *params = ConvertFromGLuint<ParamType>(texture->getBaseLevel());
            break;
        case GL_TEXTURE_MAX_LEVEL:
            *params = ConvertFromGLuint<ParamType>(texture->getMaxLevel());
            break;
        case GL_TEXTURE_MIN_LOD:
            *params = ConvertFromGLfloat<ParamType>(texture->getSamplerState().minLod);
            break;
        case GL_TEXTURE_MAX_LOD:
            *params = ConvertFromGLfloat<ParamType>(texture->getSamplerState().maxLod);
            break;
        case GL_TEXTURE_COMPARE_MODE:
            *params = ConvertFromGLenum<ParamType>(texture->getCompareMode());
            break;
        case GL_TEXTURE_COMPARE_FUNC:
            *params = ConvertFromGLenum<ParamType>(texture->getCompareFunc());
            break;
        case GL_TEXTURE_SRGB_DECODE_EXT:
            *params = ConvertFromGLenum<ParamType>(texture->getSRGBDecode());
            break;
        default:
            UNREACHABLE();
            break;
    }
}

template <typename ParamType>
void SetTexParameterBase(Context *context, Texture *texture, GLenum pname, const ParamType *params)
{
    ASSERT(texture != nullptr);

    switch (pname)
    {
        case GL_TEXTURE_WRAP_S:
            texture->setWrapS(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_WRAP_T:
            texture->setWrapT(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_WRAP_R:
            texture->setWrapR(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MIN_FILTER:
            texture->setMinFilter(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MAG_FILTER:
            texture->setMagFilter(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_USAGE_ANGLE:
            texture->setUsage(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
            texture->setMaxAnisotropy(ConvertToGLfloat(params[0]));
            break;
        case GL_TEXTURE_COMPARE_MODE:
            texture->setCompareMode(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_COMPARE_FUNC:
            texture->setCompareFunc(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_SWIZZLE_R:
            texture->setSwizzleRed(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_SWIZZLE_G:
            texture->setSwizzleGreen(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_SWIZZLE_B:
            texture->setSwizzleBlue(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_SWIZZLE_A:
            texture->setSwizzleAlpha(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_BASE_LEVEL:
        {
            context->handleError(texture->setBaseLevel(context, ConvertToGLuint(params[0])));
            break;
        }
        case GL_TEXTURE_MAX_LEVEL:
            texture->setMaxLevel(ConvertToGLuint(params[0]));
            break;
        case GL_TEXTURE_MIN_LOD:
            texture->setMinLod(ConvertToGLfloat(params[0]));
            break;
        case GL_TEXTURE_MAX_LOD:
            texture->setMaxLod(ConvertToGLfloat(params[0]));
            break;
        case GL_DEPTH_STENCIL_TEXTURE_MODE:
            texture->setDepthStencilTextureMode(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_SRGB_DECODE_EXT:
            texture->setSRGBDecode(ConvertToGLenum(params[0]));
            break;
        default:
            UNREACHABLE();
            break;
    }
}

template <typename ParamType>
void QuerySamplerParameterBase(const Sampler *sampler, GLenum pname, ParamType *params)
{
    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER:
            *params = ConvertFromGLenum<ParamType>(sampler->getMinFilter());
            break;
        case GL_TEXTURE_MAG_FILTER:
            *params = ConvertFromGLenum<ParamType>(sampler->getMagFilter());
            break;
        case GL_TEXTURE_WRAP_S:
            *params = ConvertFromGLenum<ParamType>(sampler->getWrapS());
            break;
        case GL_TEXTURE_WRAP_T:
            *params = ConvertFromGLenum<ParamType>(sampler->getWrapT());
            break;
        case GL_TEXTURE_WRAP_R:
            *params = ConvertFromGLenum<ParamType>(sampler->getWrapR());
            break;
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
            *params = ConvertFromGLfloat<ParamType>(sampler->getMaxAnisotropy());
            break;
        case GL_TEXTURE_MIN_LOD:
            *params = ConvertFromGLfloat<ParamType>(sampler->getMinLod());
            break;
        case GL_TEXTURE_MAX_LOD:
            *params = ConvertFromGLfloat<ParamType>(sampler->getMaxLod());
            break;
        case GL_TEXTURE_COMPARE_MODE:
            *params = ConvertFromGLenum<ParamType>(sampler->getCompareMode());
            break;
        case GL_TEXTURE_COMPARE_FUNC:
            *params = ConvertFromGLenum<ParamType>(sampler->getCompareFunc());
            break;
        case GL_TEXTURE_SRGB_DECODE_EXT:
            *params = ConvertFromGLenum<ParamType>(sampler->getSRGBDecode());
            break;
        default:
            UNREACHABLE();
            break;
    }
}

template <typename ParamType>
void SetSamplerParameterBase(Sampler *sampler, GLenum pname, const ParamType *params)
{
    switch (pname)
    {
        case GL_TEXTURE_WRAP_S:
            sampler->setWrapS(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_WRAP_T:
            sampler->setWrapT(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_WRAP_R:
            sampler->setWrapR(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MIN_FILTER:
            sampler->setMinFilter(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MAG_FILTER:
            sampler->setMagFilter(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MAX_ANISOTROPY_EXT:
            sampler->setMaxAnisotropy(ConvertToGLfloat(params[0]));
            break;
        case GL_TEXTURE_COMPARE_MODE:
            sampler->setCompareMode(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_COMPARE_FUNC:
            sampler->setCompareFunc(ConvertToGLenum(params[0]));
            break;
        case GL_TEXTURE_MIN_LOD:
            sampler->setMinLod(ConvertToGLfloat(params[0]));
            break;
        case GL_TEXTURE_MAX_LOD:
            sampler->setMaxLod(ConvertToGLfloat(params[0]));
            break;
        case GL_TEXTURE_SRGB_DECODE_EXT:
            sampler->setSRGBDecode(ConvertToGLenum(params[0]));
            break;
        default:
            UNREACHABLE();
            break;
    }
}

template <typename ParamType, typename CurrentDataType>
ParamType ConvertCurrentValue(CurrentDataType currentValue)
{
    return static_cast<ParamType>(currentValue);
}

template <>
GLint ConvertCurrentValue(GLfloat currentValue)
{
    return iround<GLint>(currentValue);
}

// Warning: you should ensure binding really matches attrib.bindingIndex before using this function.
template <typename ParamType, typename CurrentDataType, size_t CurrentValueCount>
void QueryVertexAttribBase(const VertexAttribute &attrib,
                           const VertexBinding &binding,
                           const CurrentDataType (&currentValueData)[CurrentValueCount],
                           GLenum pname,
                           ParamType *params)
{
    switch (pname)
    {
        case GL_CURRENT_VERTEX_ATTRIB:
            for (size_t i = 0; i < CurrentValueCount; ++i)
            {
                params[i] = ConvertCurrentValue<ParamType>(currentValueData[i]);
            }
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *params = ConvertFromGLboolean<ParamType>(attrib.enabled);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *params = ConvertFromGLuint<ParamType>(attrib.size);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *params = ConvertFromGLuint<ParamType>(attrib.vertexAttribArrayStride);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *params = ConvertFromGLenum<ParamType>(attrib.type);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *params = ConvertFromGLboolean<ParamType>(attrib.normalized);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *params = ConvertFromGLuint<ParamType>(binding.getBuffer().id());
            break;
        case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
            *params = ConvertFromGLuint<ParamType>(binding.getDivisor());
            break;
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
            *params = ConvertFromGLboolean<ParamType>(attrib.pureInteger);
            break;
        case GL_VERTEX_ATTRIB_BINDING:
            *params = ConvertFromGLuint<ParamType>(attrib.bindingIndex);
            break;
        case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:
            *params = ConvertFromGLuint<ParamType>(attrib.relativeOffset);
            break;
        default:
            UNREACHABLE();
            break;
    }
}

template <typename ParamType>
void QueryBufferParameterBase(const Buffer *buffer, GLenum pname, ParamType *params)
{
    ASSERT(buffer != nullptr);

    switch (pname)
    {
        case GL_BUFFER_USAGE:
            *params = ConvertFromGLenum<ParamType>(buffer->getUsage());
            break;
        case GL_BUFFER_SIZE:
            *params = ConvertFromGLint64<ParamType>(buffer->getSize());
            break;
        case GL_BUFFER_ACCESS_FLAGS:
            *params = ConvertFromGLuint<ParamType>(buffer->getAccessFlags());
            break;
        case GL_BUFFER_ACCESS_OES:
            *params = ConvertFromGLenum<ParamType>(buffer->getAccess());
            break;
        case GL_BUFFER_MAPPED:
            *params = ConvertFromGLboolean<ParamType>(buffer->isMapped());
            break;
        case GL_BUFFER_MAP_OFFSET:
            *params = ConvertFromGLint64<ParamType>(buffer->getMapOffset());
            break;
        case GL_BUFFER_MAP_LENGTH:
            *params = ConvertFromGLint64<ParamType>(buffer->getMapLength());
            break;
        default:
            UNREACHABLE();
            break;
    }
}

GLint GetLocationVariableProperty(const sh::VariableWithLocation &var, GLenum prop)
{
    switch (prop)
    {
        case GL_TYPE:
            return ConvertToGLint(var.type);

        case GL_ARRAY_SIZE:
            // TODO(jie.a.chen@intel.com): check array of array.
            if (var.isArray() && !var.isStruct())
            {
                return ConvertToGLint(var.elementCount());
            }
            return 1;

        case GL_NAME_LENGTH:
        {
            size_t length = var.name.size();
            if (var.isArray())
            {
                // Counts "[0]".
                length += 3;
            }
            // ES31 spec p84: This counts the terminating null char.
            ++length;
            return ConvertToGLint(length);
        }

        case GL_LOCATION:
            return var.location;

        default:
            UNREACHABLE();
            return GL_INVALID_VALUE;
    }
}

GLint GetInputResourceProperty(const Program *program, GLuint index, GLenum prop)
{
    const auto &attribute = program->getInputResource(index);
    switch (prop)
    {
        case GL_TYPE:
        case GL_ARRAY_SIZE:
        case GL_LOCATION:
        case GL_NAME_LENGTH:
            return GetLocationVariableProperty(attribute, prop);

        case GL_REFERENCED_BY_VERTEX_SHADER:
            return 1;

        case GL_REFERENCED_BY_FRAGMENT_SHADER:
        case GL_REFERENCED_BY_COMPUTE_SHADER:
            return 0;

        default:
            UNREACHABLE();
            return GL_INVALID_VALUE;
    }
}

GLint GetOutputResourceProperty(const Program *program, GLuint index, const GLenum prop)
{
    const auto &outputVariable = program->getOutputResource(index);
    switch (prop)
    {
        case GL_TYPE:
        case GL_ARRAY_SIZE:
        case GL_LOCATION:
        case GL_NAME_LENGTH:
            return GetLocationVariableProperty(outputVariable, prop);

        case GL_REFERENCED_BY_VERTEX_SHADER:
            return 0;

        case GL_REFERENCED_BY_FRAGMENT_SHADER:
            return 1;

        case GL_REFERENCED_BY_COMPUTE_SHADER:
            return 0;

        default:
            UNREACHABLE();
            return GL_INVALID_VALUE;
    }
}

GLint QueryProgramInterfaceActiveResources(const Program *program, GLenum programInterface)
{
    switch (programInterface)
    {
        case GL_PROGRAM_INPUT:
            return ConvertToGLint(program->getAttributes().size());

        case GL_PROGRAM_OUTPUT:
            return ConvertToGLint(program->getState().getOutputVariables().size());

        case GL_UNIFORM:
            return ConvertToGLint(program->getState().getUniforms().size());

        case GL_UNIFORM_BLOCK:
            return ConvertToGLint(program->getState().getUniformBlocks().size());

        // TODO(jie.a.chen@intel.com): more interfaces.
        case GL_TRANSFORM_FEEDBACK_VARYING:
        case GL_BUFFER_VARIABLE:
        case GL_SHADER_STORAGE_BLOCK:
        case GL_ATOMIC_COUNTER_BUFFER:
            UNIMPLEMENTED();
            return 0;

        default:
            UNREACHABLE();
            return 0;
    }
}

template <typename T, typename M>
GLint FindMaxSize(const std::vector<T> &resources, M member)
{
    GLint max = 0;
    for (const T &resource : resources)
    {
        max = std::max(max, ConvertToGLint((resource.*member).size()));
    }
    return max;
}

GLint QueryProgramInterfaceMaxNameLength(const Program *program, GLenum programInterface)
{
    GLint maxNameLength = 0;
    switch (programInterface)
    {
        case GL_PROGRAM_INPUT:
            maxNameLength = FindMaxSize(program->getAttributes(), &sh::Attribute::name);
            break;

        case GL_PROGRAM_OUTPUT:
            maxNameLength =
                FindMaxSize(program->getState().getOutputVariables(), &sh::OutputVariable::name);
            break;

        case GL_UNIFORM:
            maxNameLength = FindMaxSize(program->getState().getUniforms(), &LinkedUniform::name);
            break;

        case GL_UNIFORM_BLOCK:
            maxNameLength =
                FindMaxSize(program->getState().getUniformBlocks(), &InterfaceBlock::name);
            break;

        // TODO(jie.a.chen@intel.com): more interfaces.
        case GL_TRANSFORM_FEEDBACK_VARYING:
        case GL_BUFFER_VARIABLE:
        case GL_SHADER_STORAGE_BLOCK:
            UNIMPLEMENTED();
            return 0;

        default:
            UNREACHABLE();
            return 0;
    }
    // This length includes an extra character for the null terminator.
    return (maxNameLength == 0 ? 0 : maxNameLength + 1);
}

GLint QueryProgramInterfaceMaxNumActiveVariables(const Program *program, GLenum programInterface)
{
    switch (programInterface)
    {
        case GL_UNIFORM_BLOCK:
            return FindMaxSize(program->getState().getUniformBlocks(),
                               &InterfaceBlock::memberIndexes);

        // TODO(jie.a.chen@intel.com): more interfaces.
        case GL_SHADER_STORAGE_BLOCK:
        case GL_ATOMIC_COUNTER_BUFFER:
            UNIMPLEMENTED();
            return 0;

        default:
            UNREACHABLE();
            return 0;
    }
}

}  // anonymous namespace

void QueryFramebufferAttachmentParameteriv(const Framebuffer *framebuffer,
                                           GLenum attachment,
                                           GLenum pname,
                                           GLint *params)
{
    ASSERT(framebuffer);

    const FramebufferAttachment *attachmentObject = framebuffer->getAttachment(attachment);
    if (attachmentObject == nullptr)
    {
        // ES 2.0.25 spec pg 127 states that if the value of FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
        // is NONE, then querying any other pname will generate INVALID_ENUM.

        // ES 3.0.2 spec pg 235 states that if the attachment type is none,
        // GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME will return zero and be an
        // INVALID_OPERATION for all other pnames

        switch (pname)
        {
            case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
                *params = GL_NONE;
                break;

            case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
                *params = 0;
                break;

            default:
                UNREACHABLE();
                break;
        }

        return;
    }

    switch (pname)
    {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            *params = attachmentObject->type();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            *params = attachmentObject->id();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            *params = attachmentObject->mipLevel();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
            *params = attachmentObject->cubeMapFace();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
            *params = attachmentObject->getRedSize();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
            *params = attachmentObject->getGreenSize();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
            *params = attachmentObject->getBlueSize();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
            *params = attachmentObject->getAlphaSize();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
            *params = attachmentObject->getDepthSize();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
            *params = attachmentObject->getStencilSize();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
            *params = attachmentObject->getComponentType();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
            *params = attachmentObject->getColorEncoding();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
            *params = attachmentObject->layer();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_ANGLE:
            *params = attachmentObject->getNumViews();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_MULTIVIEW_LAYOUT_ANGLE:
            *params = static_cast<GLint>(attachmentObject->getMultiviewLayout());
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_ANGLE:
            *params = attachmentObject->getBaseViewIndex();
            break;

        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_VIEWPORT_OFFSETS_ANGLE:
        {
            const std::vector<Offset> &offsets = attachmentObject->getMultiviewViewportOffsets();
            for (size_t i = 0u; i < offsets.size(); ++i)
            {
                params[i * 2u]      = offsets[i].x;
                params[i * 2u + 1u] = offsets[i].y;
            }
        }
        break;

        default:
            UNREACHABLE();
            break;
    }
}

void QueryBufferParameteriv(const Buffer *buffer, GLenum pname, GLint *params)
{
    QueryBufferParameterBase(buffer, pname, params);
}

void QueryBufferParameteri64v(const Buffer *buffer, GLenum pname, GLint64 *params)
{
    QueryBufferParameterBase(buffer, pname, params);
}

void QueryBufferPointerv(const Buffer *buffer, GLenum pname, void **params)
{
    switch (pname)
    {
        case GL_BUFFER_MAP_POINTER:
            *params = buffer->getMapPointer();
            break;

        default:
            UNREACHABLE();
            break;
    }
}

void QueryProgramiv(const Context *context, const Program *program, GLenum pname, GLint *params)
{
    ASSERT(program != nullptr);

    switch (pname)
    {
        case GL_DELETE_STATUS:
            *params = program->isFlaggedForDeletion();
            return;
        case GL_LINK_STATUS:
            *params = program->isLinked();
            return;
        case GL_VALIDATE_STATUS:
            *params = program->isValidated();
            return;
        case GL_INFO_LOG_LENGTH:
            *params = program->getInfoLogLength();
            return;
        case GL_ATTACHED_SHADERS:
            *params = program->getAttachedShadersCount();
            return;
        case GL_ACTIVE_ATTRIBUTES:
            *params = program->getActiveAttributeCount();
            return;
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
            *params = program->getActiveAttributeMaxLength();
            return;
        case GL_ACTIVE_UNIFORMS:
            *params = program->getActiveUniformCount();
            return;
        case GL_ACTIVE_UNIFORM_MAX_LENGTH:
            *params = program->getActiveUniformMaxLength();
            return;
        case GL_PROGRAM_BINARY_LENGTH_OES:
            *params = program->getBinaryLength(context);
            return;
        case GL_ACTIVE_UNIFORM_BLOCKS:
            *params = program->getActiveUniformBlockCount();
            return;
        case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH:
            *params = program->getActiveUniformBlockMaxLength();
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
            *params = program->getTransformFeedbackBufferMode();
            break;
        case GL_TRANSFORM_FEEDBACK_VARYINGS:
            *params = program->getTransformFeedbackVaryingCount();
            break;
        case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH:
            *params = program->getTransformFeedbackVaryingMaxLength();
            break;
        case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
            *params = program->getBinaryRetrievableHint();
            break;
        case GL_PROGRAM_SEPARABLE:
            *params = program->isSeparable();
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void QueryRenderbufferiv(const Context *context,
                         const Renderbuffer *renderbuffer,
                         GLenum pname,
                         GLint *params)
{
    ASSERT(renderbuffer != nullptr);

    switch (pname)
    {
        case GL_RENDERBUFFER_WIDTH:
            *params = renderbuffer->getWidth();
            break;
        case GL_RENDERBUFFER_HEIGHT:
            *params = renderbuffer->getHeight();
            break;
        case GL_RENDERBUFFER_INTERNAL_FORMAT:
            // Special case the WebGL 1 DEPTH_STENCIL format.
            if (context->isWebGL1() &&
                renderbuffer->getFormat().info->internalFormat == GL_DEPTH24_STENCIL8)
            {
                *params = GL_DEPTH_STENCIL;
            }
            else
            {
                *params = renderbuffer->getFormat().info->internalFormat;
            }
            break;
        case GL_RENDERBUFFER_RED_SIZE:
            *params = renderbuffer->getRedSize();
            break;
        case GL_RENDERBUFFER_GREEN_SIZE:
            *params = renderbuffer->getGreenSize();
            break;
        case GL_RENDERBUFFER_BLUE_SIZE:
            *params = renderbuffer->getBlueSize();
            break;
        case GL_RENDERBUFFER_ALPHA_SIZE:
            *params = renderbuffer->getAlphaSize();
            break;
        case GL_RENDERBUFFER_DEPTH_SIZE:
            *params = renderbuffer->getDepthSize();
            break;
        case GL_RENDERBUFFER_STENCIL_SIZE:
            *params = renderbuffer->getStencilSize();
            break;
        case GL_RENDERBUFFER_SAMPLES_ANGLE:
            *params = renderbuffer->getSamples();
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void QueryShaderiv(const Context *context, Shader *shader, GLenum pname, GLint *params)
{
    ASSERT(shader != nullptr);

    switch (pname)
    {
        case GL_SHADER_TYPE:
            *params = shader->getType();
            return;
        case GL_DELETE_STATUS:
            *params = shader->isFlaggedForDeletion();
            return;
        case GL_COMPILE_STATUS:
            *params = shader->isCompiled(context) ? GL_TRUE : GL_FALSE;
            return;
        case GL_INFO_LOG_LENGTH:
            *params = shader->getInfoLogLength(context);
            return;
        case GL_SHADER_SOURCE_LENGTH:
            *params = shader->getSourceLength();
            return;
        case GL_TRANSLATED_SHADER_SOURCE_LENGTH_ANGLE:
            *params = shader->getTranslatedSourceWithDebugInfoLength(context);
            return;
        default:
            UNREACHABLE();
            break;
    }
}

void QueryTexLevelParameterfv(const Texture *texture,
                              GLenum target,
                              GLint level,
                              GLenum pname,
                              GLfloat *params)
{
    QueryTexLevelParameterBase(texture, target, level, pname, params);
}

void QueryTexLevelParameteriv(const Texture *texture,
                              GLenum target,
                              GLint level,
                              GLenum pname,
                              GLint *params)
{
    QueryTexLevelParameterBase(texture, target, level, pname, params);
}

void QueryTexParameterfv(const Texture *texture, GLenum pname, GLfloat *params)
{
    QueryTexParameterBase(texture, pname, params);
}

void QueryTexParameteriv(const Texture *texture, GLenum pname, GLint *params)
{
    QueryTexParameterBase(texture, pname, params);
}

void QuerySamplerParameterfv(const Sampler *sampler, GLenum pname, GLfloat *params)
{
    QuerySamplerParameterBase(sampler, pname, params);
}

void QuerySamplerParameteriv(const Sampler *sampler, GLenum pname, GLint *params)
{
    QuerySamplerParameterBase(sampler, pname, params);
}

void QueryVertexAttribfv(const VertexAttribute &attrib,
                         const VertexBinding &binding,
                         const VertexAttribCurrentValueData &currentValueData,
                         GLenum pname,
                         GLfloat *params)
{
    QueryVertexAttribBase(attrib, binding, currentValueData.FloatValues, pname, params);
}

void QueryVertexAttribiv(const VertexAttribute &attrib,
                         const VertexBinding &binding,
                         const VertexAttribCurrentValueData &currentValueData,
                         GLenum pname,
                         GLint *params)
{
    QueryVertexAttribBase(attrib, binding, currentValueData.FloatValues, pname, params);
}

void QueryVertexAttribPointerv(const VertexAttribute &attrib, GLenum pname, void **pointer)
{
    switch (pname)
    {
        case GL_VERTEX_ATTRIB_ARRAY_POINTER:
            *pointer = const_cast<void *>(attrib.pointer);
            break;

        default:
            UNREACHABLE();
            break;
    }
}

void QueryVertexAttribIiv(const VertexAttribute &attrib,
                          const VertexBinding &binding,
                          const VertexAttribCurrentValueData &currentValueData,
                          GLenum pname,
                          GLint *params)
{
    QueryVertexAttribBase(attrib, binding, currentValueData.IntValues, pname, params);
}

void QueryVertexAttribIuiv(const VertexAttribute &attrib,
                           const VertexBinding &binding,
                           const VertexAttribCurrentValueData &currentValueData,
                           GLenum pname,
                           GLuint *params)
{
    QueryVertexAttribBase(attrib, binding, currentValueData.UnsignedIntValues, pname, params);
}

void QueryActiveUniformBlockiv(const Program *program,
                               GLuint uniformBlockIndex,
                               GLenum pname,
                               GLint *params)
{
    const InterfaceBlock &uniformBlock = program->getUniformBlockByIndex(uniformBlockIndex);
    switch (pname)
    {
        case GL_UNIFORM_BLOCK_BINDING:
            *params = ConvertToGLint(program->getUniformBlockBinding(uniformBlockIndex));
            break;
        case GL_UNIFORM_BLOCK_DATA_SIZE:
            *params = ConvertToGLint(uniformBlock.dataSize);
            break;
        case GL_UNIFORM_BLOCK_NAME_LENGTH:
            *params = ConvertToGLint(uniformBlock.nameWithArrayIndex().size() + 1);
            break;
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
            *params = ConvertToGLint(uniformBlock.memberIndexes.size());
            break;
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES:
            for (size_t blockMemberIndex = 0; blockMemberIndex < uniformBlock.memberIndexes.size();
                 blockMemberIndex++)
            {
                params[blockMemberIndex] =
                    ConvertToGLint(uniformBlock.memberIndexes[blockMemberIndex]);
            }
            break;
        case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
            *params = ConvertToGLint(uniformBlock.vertexStaticUse);
            break;
        case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
            *params = ConvertToGLint(uniformBlock.fragmentStaticUse);
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void QueryInternalFormativ(const TextureCaps &format, GLenum pname, GLsizei bufSize, GLint *params)
{
    switch (pname)
    {
        case GL_NUM_SAMPLE_COUNTS:
            if (bufSize != 0)
            {
                *params = static_cast<GLint>(format.sampleCounts.size());
            }
            break;

        case GL_SAMPLES:
        {
            size_t returnCount   = std::min<size_t>(bufSize, format.sampleCounts.size());
            auto sampleReverseIt = format.sampleCounts.rbegin();
            for (size_t sampleIndex = 0; sampleIndex < returnCount; ++sampleIndex)
            {
                params[sampleIndex] = *sampleReverseIt++;
            }
        }
        break;

        default:
            UNREACHABLE();
            break;
    }
}

void QueryFramebufferParameteriv(const Framebuffer *framebuffer, GLenum pname, GLint *params)
{
    ASSERT(framebuffer);

    switch (pname)
    {
        case GL_FRAMEBUFFER_DEFAULT_WIDTH:
            *params = framebuffer->getDefaultWidth();
            break;
        case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
            *params = framebuffer->getDefaultHeight();
            break;
        case GL_FRAMEBUFFER_DEFAULT_SAMPLES:
            *params = framebuffer->getDefaultSamples();
            break;
        case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLE_LOCATIONS:
            *params = framebuffer->getDefaultFixedSampleLocations();
            break;
        default:
            UNREACHABLE();
            break;
    }
}

Error QuerySynciv(const Sync *sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values)
{
    ASSERT(sync);

    // All queries return one value, exit early if the buffer can't fit anything.
    if (bufSize < 1)
    {
        if (length != nullptr)
        {
            *length = 0;
        }
        return NoError();
    }

    switch (pname)
    {
        case GL_OBJECT_TYPE:
            *values = ConvertToGLint(GL_SYNC_FENCE);
            break;
        case GL_SYNC_CONDITION:
            *values = ConvertToGLint(sync->getCondition());
            break;
        case GL_SYNC_FLAGS:
            *values = ConvertToGLint(sync->getFlags());
            break;
        case GL_SYNC_STATUS:
            ANGLE_TRY(sync->getStatus(values));
            break;

        default:
            UNREACHABLE();
            break;
    }

    if (length != nullptr)
    {
        *length = 1;
    }

    return NoError();
}

void SetTexParameterf(Context *context, Texture *texture, GLenum pname, GLfloat param)
{
    SetTexParameterBase(context, texture, pname, &param);
}

void SetTexParameterfv(Context *context, Texture *texture, GLenum pname, const GLfloat *params)
{
    SetTexParameterBase(context, texture, pname, params);
}

void SetTexParameteri(Context *context, Texture *texture, GLenum pname, GLint param)
{
    SetTexParameterBase(context, texture, pname, &param);
}

void SetTexParameteriv(Context *context, Texture *texture, GLenum pname, const GLint *params)
{
    SetTexParameterBase(context, texture, pname, params);
}

void SetSamplerParameterf(Sampler *sampler, GLenum pname, GLfloat param)
{
    SetSamplerParameterBase(sampler, pname, &param);
}

void SetSamplerParameterfv(Sampler *sampler, GLenum pname, const GLfloat *params)
{
    SetSamplerParameterBase(sampler, pname, params);
}

void SetSamplerParameteri(Sampler *sampler, GLenum pname, GLint param)
{
    SetSamplerParameterBase(sampler, pname, &param);
}

void SetSamplerParameteriv(Sampler *sampler, GLenum pname, const GLint *params)
{
    SetSamplerParameterBase(sampler, pname, params);
}

void SetFramebufferParameteri(Framebuffer *framebuffer, GLenum pname, GLint param)
{
    ASSERT(framebuffer);

    switch (pname)
    {
        case GL_FRAMEBUFFER_DEFAULT_WIDTH:
            framebuffer->setDefaultWidth(param);
            break;
        case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
            framebuffer->setDefaultHeight(param);
            break;
        case GL_FRAMEBUFFER_DEFAULT_SAMPLES:
            framebuffer->setDefaultSamples(param);
            break;
        case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLE_LOCATIONS:
            framebuffer->setDefaultFixedSampleLocations(static_cast<GLboolean>(param));
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void SetProgramParameteri(Program *program, GLenum pname, GLint value)
{
    ASSERT(program);

    switch (pname)
    {
        case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
            program->setBinaryRetrievableHint(value != GL_FALSE);
            break;
        case GL_PROGRAM_SEPARABLE:
            program->setSeparable(value != GL_FALSE);
            break;
        default:
            UNREACHABLE();
            break;
    }
}

GLuint QueryProgramResourceIndex(const Program *program,
                                 GLenum programInterface,
                                 const GLchar *name)
{
    switch (programInterface)
    {
        case GL_PROGRAM_INPUT:
            return program->getInputResourceIndex(name);

        case GL_PROGRAM_OUTPUT:
            return program->getOutputResourceIndex(name);

        // TODO(jie.a.chen@intel.com): more interfaces.
        case GL_UNIFORM:
        case GL_UNIFORM_BLOCK:
        case GL_TRANSFORM_FEEDBACK_VARYING:
        case GL_BUFFER_VARIABLE:
        case GL_SHADER_STORAGE_BLOCK:
            UNIMPLEMENTED();
            return GL_INVALID_INDEX;

        default:
            UNREACHABLE();
            return GL_INVALID_INDEX;
    }
}

void QueryProgramResourceName(const Program *program,
                              GLenum programInterface,
                              GLuint index,
                              GLsizei bufSize,
                              GLsizei *length,
                              GLchar *name)
{
    switch (programInterface)
    {
        case GL_PROGRAM_INPUT:
            program->getInputResourceName(index, bufSize, length, name);
            break;

        case GL_PROGRAM_OUTPUT:
            program->getOutputResourceName(index, bufSize, length, name);
            break;

        // TODO(jie.a.chen@intel.com): more interfaces.
        case GL_UNIFORM:
        case GL_UNIFORM_BLOCK:
        case GL_TRANSFORM_FEEDBACK_VARYING:
        case GL_BUFFER_VARIABLE:
        case GL_SHADER_STORAGE_BLOCK:
            UNIMPLEMENTED();
            break;

        default:
            UNREACHABLE();
    }
}

GLint QueryProgramResourceLocation(const Program *program,
                                   GLenum programInterface,
                                   const GLchar *name)
{
    switch (programInterface)
    {
        case GL_PROGRAM_INPUT:
            return program->getAttributeLocation(name);

        case GL_PROGRAM_OUTPUT:
            return program->getFragDataLocation(name);

        // TODO(jie.a.chen@intel.com): more interfaces.
        case GL_UNIFORM:
        case GL_UNIFORM_BLOCK:
        case GL_TRANSFORM_FEEDBACK_VARYING:
        case GL_BUFFER_VARIABLE:
        case GL_SHADER_STORAGE_BLOCK:
            UNIMPLEMENTED();
            return -1;

        default:
            UNREACHABLE();
            return -1;
    }
}

void QueryProgramResourceiv(const Program *program,
                            GLenum programInterface,
                            GLuint index,
                            GLsizei propCount,
                            const GLenum *props,
                            GLsizei bufSize,
                            GLsizei *length,
                            GLint *params)
{
    if (!program->isLinked())
    {
        if (length != nullptr)
        {
            *length = 0;
        }
        return;
    }

    GLsizei count = std::min(propCount, bufSize);
    if (length != nullptr)
    {
        *length = count;
    }

    for (GLsizei i = 0; i < count; i++)
    {
        switch (programInterface)
        {
            case GL_PROGRAM_INPUT:
                params[i] = GetInputResourceProperty(program, index, props[i]);
                break;

            case GL_PROGRAM_OUTPUT:
                params[i] = GetOutputResourceProperty(program, index, props[i]);
                break;

            // TODO(jie.a.chen@intel.com): more interfaces.
            case GL_UNIFORM:
            case GL_UNIFORM_BLOCK:
            case GL_TRANSFORM_FEEDBACK_VARYING:
            case GL_BUFFER_VARIABLE:
            case GL_SHADER_STORAGE_BLOCK:
            case GL_ATOMIC_COUNTER_BUFFER:
                UNIMPLEMENTED();
                params[i] = GL_INVALID_VALUE;
                break;

            default:
                UNREACHABLE();
                params[i] = GL_INVALID_VALUE;
        }
    }
}

void QueryProgramInterfaceiv(const Program *program,
                             GLenum programInterface,
                             GLenum pname,
                             GLint *params)
{
    switch (pname)
    {
        case GL_ACTIVE_RESOURCES:
            *params = QueryProgramInterfaceActiveResources(program, programInterface);
            break;

        case GL_MAX_NAME_LENGTH:
            *params = QueryProgramInterfaceMaxNameLength(program, programInterface);
            break;

        case GL_MAX_NUM_ACTIVE_VARIABLES:
            *params = QueryProgramInterfaceMaxNumActiveVariables(program, programInterface);
            break;

        default:
            UNREACHABLE();
    }
}

}  // namespace gl

namespace egl
{

void QueryConfigAttrib(const Config *config, EGLint attribute, EGLint *value)
{
    ASSERT(config != nullptr);
    switch (attribute)
    {
        case EGL_BUFFER_SIZE:
            *value = config->bufferSize;
            break;
        case EGL_ALPHA_SIZE:
            *value = config->alphaSize;
            break;
        case EGL_BLUE_SIZE:
            *value = config->blueSize;
            break;
        case EGL_GREEN_SIZE:
            *value = config->greenSize;
            break;
        case EGL_RED_SIZE:
            *value = config->redSize;
            break;
        case EGL_DEPTH_SIZE:
            *value = config->depthSize;
            break;
        case EGL_STENCIL_SIZE:
            *value = config->stencilSize;
            break;
        case EGL_CONFIG_CAVEAT:
            *value = config->configCaveat;
            break;
        case EGL_CONFIG_ID:
            *value = config->configID;
            break;
        case EGL_LEVEL:
            *value = config->level;
            break;
        case EGL_NATIVE_RENDERABLE:
            *value = config->nativeRenderable;
            break;
        case EGL_NATIVE_VISUAL_ID:
            *value = config->nativeVisualID;
            break;
        case EGL_NATIVE_VISUAL_TYPE:
            *value = config->nativeVisualType;
            break;
        case EGL_SAMPLES:
            *value = config->samples;
            break;
        case EGL_SAMPLE_BUFFERS:
            *value = config->sampleBuffers;
            break;
        case EGL_SURFACE_TYPE:
            *value = config->surfaceType;
            break;
        case EGL_TRANSPARENT_TYPE:
            *value = config->transparentType;
            break;
        case EGL_TRANSPARENT_BLUE_VALUE:
            *value = config->transparentBlueValue;
            break;
        case EGL_TRANSPARENT_GREEN_VALUE:
            *value = config->transparentGreenValue;
            break;
        case EGL_TRANSPARENT_RED_VALUE:
            *value = config->transparentRedValue;
            break;
        case EGL_BIND_TO_TEXTURE_RGB:
            *value = config->bindToTextureRGB;
            break;
        case EGL_BIND_TO_TEXTURE_RGBA:
            *value = config->bindToTextureRGBA;
            break;
        case EGL_MIN_SWAP_INTERVAL:
            *value = config->minSwapInterval;
            break;
        case EGL_MAX_SWAP_INTERVAL:
            *value = config->maxSwapInterval;
            break;
        case EGL_LUMINANCE_SIZE:
            *value = config->luminanceSize;
            break;
        case EGL_ALPHA_MASK_SIZE:
            *value = config->alphaMaskSize;
            break;
        case EGL_COLOR_BUFFER_TYPE:
            *value = config->colorBufferType;
            break;
        case EGL_RENDERABLE_TYPE:
            *value = config->renderableType;
            break;
        case EGL_MATCH_NATIVE_PIXMAP:
            *value = false;
            UNIMPLEMENTED();
            break;
        case EGL_CONFORMANT:
            *value = config->conformant;
            break;
        case EGL_MAX_PBUFFER_WIDTH:
            *value = config->maxPBufferWidth;
            break;
        case EGL_MAX_PBUFFER_HEIGHT:
            *value = config->maxPBufferHeight;
            break;
        case EGL_MAX_PBUFFER_PIXELS:
            *value = config->maxPBufferPixels;
            break;
        case EGL_OPTIMAL_SURFACE_ORIENTATION_ANGLE:
            *value = config->optimalOrientation;
            break;
        case EGL_COLOR_COMPONENT_TYPE_EXT:
            *value = config->colorComponentType;
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void QuerySurfaceAttrib(const Surface *surface, EGLint attribute, EGLint *value)
{
    switch (attribute)
    {
        case EGL_GL_COLORSPACE:
            *value = surface->getGLColorspace();
            break;
        case EGL_VG_ALPHA_FORMAT:
            *value = surface->getVGAlphaFormat();
            break;
        case EGL_VG_COLORSPACE:
            *value = surface->getVGColorspace();
            break;
        case EGL_CONFIG_ID:
            *value = surface->getConfig()->configID;
            break;
        case EGL_HEIGHT:
            *value = surface->getHeight();
            break;
        case EGL_HORIZONTAL_RESOLUTION:
            *value = surface->getHorizontalResolution();
            break;
        case EGL_LARGEST_PBUFFER:
            // The EGL spec states that value is not written if the surface is not a pbuffer
            if (surface->getType() == EGL_PBUFFER_BIT)
            {
                *value = surface->getLargestPbuffer();
            }
            break;
        case EGL_MIPMAP_TEXTURE:
            // The EGL spec states that value is not written if the surface is not a pbuffer
            if (surface->getType() == EGL_PBUFFER_BIT)
            {
                *value = surface->getMipmapTexture();
            }
            break;
        case EGL_MIPMAP_LEVEL:
            // The EGL spec states that value is not written if the surface is not a pbuffer
            if (surface->getType() == EGL_PBUFFER_BIT)
            {
                *value = surface->getMipmapLevel();
            }
            break;
        case EGL_MULTISAMPLE_RESOLVE:
            *value = surface->getMultisampleResolve();
            break;
        case EGL_PIXEL_ASPECT_RATIO:
            *value = surface->getPixelAspectRatio();
            break;
        case EGL_RENDER_BUFFER:
            *value = surface->getRenderBuffer();
            break;
        case EGL_SWAP_BEHAVIOR:
            *value = surface->getSwapBehavior();
            break;
        case EGL_TEXTURE_FORMAT:
            // The EGL spec states that value is not written if the surface is not a pbuffer
            if (surface->getType() == EGL_PBUFFER_BIT)
            {
                *value = surface->getTextureFormat();
            }
            break;
        case EGL_TEXTURE_TARGET:
            // The EGL spec states that value is not written if the surface is not a pbuffer
            if (surface->getType() == EGL_PBUFFER_BIT)
            {
                *value = surface->getTextureTarget();
            }
            break;
        case EGL_VERTICAL_RESOLUTION:
            *value = surface->getVerticalResolution();
            break;
        case EGL_WIDTH:
            *value = surface->getWidth();
            break;
        case EGL_POST_SUB_BUFFER_SUPPORTED_NV:
            *value = surface->isPostSubBufferSupported();
            break;
        case EGL_FIXED_SIZE_ANGLE:
            *value = surface->isFixedSize();
            break;
        case EGL_FLEXIBLE_SURFACE_COMPATIBILITY_SUPPORTED_ANGLE:
            *value = surface->flexibleSurfaceCompatibilityRequested();
            break;
        case EGL_SURFACE_ORIENTATION_ANGLE:
            *value = surface->getOrientation();
            break;
        case EGL_DIRECT_COMPOSITION_ANGLE:
            *value = surface->directComposition();
            break;
        default:
            UNREACHABLE();
            break;
    }
}

void SetSurfaceAttrib(Surface *surface, EGLint attribute, EGLint value)
{
    switch (attribute)
    {
        case EGL_MIPMAP_LEVEL:
            surface->setMipmapLevel(value);
            break;
        case EGL_MULTISAMPLE_RESOLVE:
            surface->setMultisampleResolve(value);
            break;
        case EGL_SWAP_BEHAVIOR:
            surface->setSwapBehavior(value);
            break;
        default:
            UNREACHABLE();
            break;
    }
}

}  // namespace egl
