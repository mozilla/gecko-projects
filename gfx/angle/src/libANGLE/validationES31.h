//
// Copyright (c) 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// validationES31.h: Validation functions for OpenGL ES 3.1 entry point parameters

#ifndef LIBANGLE_VALIDATION_ES31_H_
#define LIBANGLE_VALIDATION_ES31_H_

#include <GLES3/gl31.h>

namespace gl
{
class Context;
class ValidationContext;

bool ValidateGetBooleani_v(Context *context, GLenum target, GLuint index, GLboolean *data);
bool ValidateGetBooleani_vRobustANGLE(Context *context,
                                      GLenum target,
                                      GLuint index,
                                      GLsizei bufSize,
                                      GLsizei *length,
                                      GLboolean *data);

bool ValidateGetTexLevelParameterfv(Context *context,
                                    GLenum target,
                                    GLint level,
                                    GLenum pname,
                                    GLfloat *params);
bool ValidateGetTexLevelParameteriv(Context *context,
                                    GLenum target,
                                    GLint level,
                                    GLenum pname,
                                    GLint *param);

bool ValidateTexStorage2DMultiSample(Context *context,
                                     GLenum target,
                                     GLsizei samples,
                                     GLint internalFormat,
                                     GLsizei width,
                                     GLsizei height,
                                     GLboolean fixedSampleLocations);
bool ValidateGetMultisamplefv(Context *context, GLenum pname, GLuint index, GLfloat *val);

bool ValidateDrawIndirectBase(Context *context, GLenum mode, const void *indirect);
bool ValidateDrawArraysIndirect(Context *context, GLenum mode, const void *indirect);
bool ValidateDrawElementsIndirect(Context *context, GLenum mode, GLenum type, const void *indirect);

bool ValidationFramebufferParameteri(Context *context, GLenum target, GLenum pname, GLint param);
bool ValidationGetFramebufferParameteri(Context *context,
                                        GLenum target,
                                        GLenum pname,
                                        GLint *params);

bool ValidateGetProgramResourceIndex(Context *context,
                                     GLuint program,
                                     GLenum programInterface,
                                     const GLchar *name);
bool ValidateGetProgramResourceName(Context *context,
                                    GLuint program,
                                    GLenum programInterface,
                                    GLuint index,
                                    GLsizei bufSize,
                                    GLsizei *length,
                                    GLchar *name);
bool ValidateGetProgramResourceLocation(Context *context,
                                        GLuint program,
                                        GLenum programInterface,
                                        const GLchar *name);

bool ValidateGetProgramResourceiv(Context *context,
                                  GLuint program,
                                  GLenum programInterface,
                                  GLuint index,
                                  GLsizei propCount,
                                  const GLenum *props,
                                  GLsizei bufSize,
                                  GLsizei *length,
                                  GLint *params);

bool ValidateGetProgramInterfaceiv(Context *context,
                                   GLuint program,
                                   GLenum programInterface,
                                   GLenum pname,
                                   GLint *params);

bool ValidateBindVertexBuffer(ValidationContext *context,
                              GLuint bindingIndex,
                              GLuint buffer,
                              GLintptr offset,
                              GLsizei stride);
bool ValidateVertexAttribFormat(ValidationContext *context,
                                GLuint attribIndex,
                                GLint size,
                                GLenum type,
                                GLuint relativeOffset,
                                GLboolean pureInteger);
bool ValidateVertexAttribBinding(ValidationContext *context,
                                 GLuint attribIndex,
                                 GLuint bindingIndex);
bool ValidateVertexBindingDivisor(ValidationContext *context, GLuint bindingIndex, GLuint divisor);

bool ValidateDispatchCompute(Context *context,
                             GLuint numGroupsX,
                             GLuint numGroupsY,
                             GLuint numGroupsZ);

bool ValidateBindImageTexture(Context *context,
                              GLuint unit,
                              GLuint texture,
                              GLint level,
                              GLboolean layered,
                              GLint layer,
                              GLenum access,
                              GLenum format);

bool ValidateGenProgramPipelines(Context *context, GLint n, GLuint *pipelines);
bool ValidateDeleteProgramPipelines(Context *context, GLint n, const GLuint *pipelines);
bool ValidateBindProgramPipeline(Context *context, GLuint pipeline);
bool ValidateIsProgramPipeline(Context *context, GLuint pipeline);

bool ValidateSampleMaski(Context *context, GLuint maskNumber);

}  // namespace gl

#endif  // LIBANGLE_VALIDATION_ES31_H_
