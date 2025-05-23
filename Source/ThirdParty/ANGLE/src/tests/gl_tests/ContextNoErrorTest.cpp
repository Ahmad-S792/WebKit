//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ContextNoErrorTest:
//   Tests pertaining to GL_KHR_no_error
//

#include <gtest/gtest.h>

#include "common/platform.h"
#include "test_utils/ANGLETest.h"
#include "test_utils/gl_raii.h"

using namespace angle;

class ContextNoErrorTest : public ANGLETest<>
{
  protected:
    ContextNoErrorTest() : mNaughtyTexture(0) { setNoErrorEnabled(true); }

    void testTearDown() override
    {
        if (mNaughtyTexture != 0)
        {
            glDeleteTextures(1, &mNaughtyTexture);
        }
    }

    void bindNaughtyTexture()
    {
        glGenTextures(1, &mNaughtyTexture);
        ASSERT_GL_NO_ERROR();
        glBindTexture(GL_TEXTURE_CUBE_MAP, mNaughtyTexture);
        ASSERT_GL_NO_ERROR();

        // mNaughtyTexture should now be a GL_TEXTURE_CUBE_MAP texture, so rebinding it to
        // GL_TEXTURE_2D is an error
        glBindTexture(GL_TEXTURE_2D, mNaughtyTexture);
    }

    GLuint mNaughtyTexture = 0;
};

class ContextNoErrorTestES3 : public ContextNoErrorTest
{};

class ContextNoErrorPPOTest31 : public ContextNoErrorTest
{
  protected:
    void testTearDown() override
    {
        glDeleteProgram(mVertProg);
        glDeleteProgram(mFragProg);
        glDeleteProgramPipelines(1, &mPipeline);
    }

    void bindProgramPipeline(const GLchar *vertString, const GLchar *fragString);
    void drawQuadWithPPO(const std::string &positionAttribName,
                         const GLfloat positionAttribZ,
                         const GLfloat positionAttribXYScale);

    GLuint mVertProg = 0;
    GLuint mFragProg = 0;
    GLuint mPipeline = 0;
};

void ContextNoErrorPPOTest31::bindProgramPipeline(const GLchar *vertString,
                                                  const GLchar *fragString)
{
    mVertProg = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &vertString);
    ASSERT_NE(mVertProg, 0u);
    mFragProg = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fragString);
    ASSERT_NE(mFragProg, 0u);

    // Generate a program pipeline and attach the programs to their respective stages
    glGenProgramPipelines(1, &mPipeline);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    EXPECT_GL_NO_ERROR();
    glBindProgramPipeline(mPipeline);
    EXPECT_GL_NO_ERROR();
}

void ContextNoErrorPPOTest31::drawQuadWithPPO(const std::string &positionAttribName,
                                              const GLfloat positionAttribZ,
                                              const GLfloat positionAttribXYScale)
{
    glUseProgram(0);

    std::array<Vector3, 6> quadVertices = ANGLETestBase::GetQuadVertices();

    for (Vector3 &vertex : quadVertices)
    {
        vertex.x() *= positionAttribXYScale;
        vertex.y() *= positionAttribXYScale;
        vertex.z() = positionAttribZ;
    }

    GLint positionLocation = glGetAttribLocation(mVertProg, positionAttribName.c_str());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(positionLocation, 3, GL_FLOAT, GL_FALSE, 0, quadVertices.data());
    glEnableVertexAttribArray(positionLocation);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(positionLocation);
    glVertexAttribPointer(positionLocation, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
}

// Tests that error reporting is suppressed when GL_KHR_no_error is enabled
TEST_P(ContextNoErrorTest, NoError)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    bindNaughtyTexture();
    EXPECT_GL_NO_ERROR();
}

// Test glDetachShader to make sure it resolves linking with a no error context and doesn't assert
TEST_P(ContextNoErrorTest, DetachAfterLink)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    GLuint vs      = CompileShader(GL_VERTEX_SHADER, essl1_shaders::vs::Simple());
    GLuint fs      = CompileShader(GL_FRAGMENT_SHADER, essl1_shaders::fs::Red());
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDetachShader(program, vs);
    glDetachShader(program, fs);

    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteProgram(program);
    EXPECT_GL_NO_ERROR();
}

// Tests that we can draw with a program pipeline when GL_KHR_no_error is enabled.
TEST_P(ContextNoErrorPPOTest31, DrawWithPPO)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = essl31_shaders::fs::Red();

    bindProgramPipeline(vertString, fragString);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test drawing with program and then with PPO to make sure it resolves linking of both the program
// and the PPO with a no error context.
TEST_P(ContextNoErrorPPOTest31, DrawWithProgramThenPPO)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    ANGLE_GL_PROGRAM(simpleProgram, essl31_shaders::vs::Simple(), essl31_shaders::fs::Red());
    ASSERT_NE(simpleProgram, 0u);
    EXPECT_GL_NO_ERROR();

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = essl31_shaders::fs::Green();

    // Bind the PPO
    bindProgramPipeline(vertString, fragString);

    // Bind the program
    glUseProgram(simpleProgram);
    EXPECT_GL_NO_ERROR();

    // Draw and expect red since program overrides PPO
    drawQuad(simpleProgram, essl31_shaders::PositionAttrib(), 0.5f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Unbind the program
    glUseProgram(0);
    EXPECT_GL_NO_ERROR();

    // Draw and expect green
    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    EXPECT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);
}

// Test glUseProgramStages with different programs
TEST_P(ContextNoErrorPPOTest31, UseProgramStagesWithDifferentPrograms)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString  = essl31_shaders::vs::Simple();
    const GLchar *fragString1 = R"(#version 310 es
precision highp float;
uniform float redColorIn;
uniform float greenColorIn;
out vec4 my_FragColor;
void main()
{
    my_FragColor = vec4(redColorIn, greenColorIn, 0.0, 1.0);
})";
    const GLchar *fragString2 = R"(#version 310 es
precision highp float;
uniform float greenColorIn;
uniform float blueColorIn;
out vec4 my_FragColor;
void main()
{
    my_FragColor = vec4(0.0, greenColorIn, blueColorIn, 1.0);
})";

    bindProgramPipeline(vertString, fragString1);

    // Set the output color to red
    GLint location = glGetUniformLocation(mFragProg, "redColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 1.0);
    location = glGetUniformLocation(mFragProg, "greenColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 0.0);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    GLuint fragProg;
    fragProg = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fragString2);
    ASSERT_NE(fragProg, 0u);
    EXPECT_GL_NO_ERROR();

    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, fragProg);
    EXPECT_GL_NO_ERROR();

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Set the output color to blue
    location = glGetUniformLocation(fragProg, "greenColorIn");
    glActiveShaderProgram(mPipeline, fragProg);
    glUniform1f(location, 0.0);
    location = glGetUniformLocation(fragProg, "blueColorIn");
    glActiveShaderProgram(mPipeline, fragProg);
    glUniform1f(location, 1.0);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::blue);

    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    EXPECT_GL_NO_ERROR();

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    glDeleteProgram(mVertProg);
    glDeleteProgram(mFragProg);
    glDeleteProgram(fragProg);
}

// Test glUseProgramStages with repeated calls to glUseProgramStages with the same programs.
TEST_P(ContextNoErrorPPOTest31, RepeatedCallToUseProgramStagesWithSamePrograms)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = R"(#version 310 es
precision highp float;
uniform float redColorIn;
uniform float greenColorIn;
out vec4 my_FragColor;
void main()
{
    my_FragColor = vec4(redColorIn, greenColorIn, 0.0, 1.0);
})";

    bindProgramPipeline(vertString, fragString);

    // Set the output color to red
    GLint location = glGetUniformLocation(mFragProg, "redColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 1.0);
    location = glGetUniformLocation(mFragProg, "greenColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 0.0);

    // These following calls to glUseProgramStages should not cause a re-link.
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    EXPECT_GL_NO_ERROR();

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    glDeleteProgram(mVertProg);
    glDeleteProgram(mFragProg);
}

// Tests that an incorrect enum to GetInteger does not cause an application crash.
TEST_P(ContextNoErrorTest, InvalidGetIntegerDoesNotCrash)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    GLint value = 1;
    glGetIntegerv(GL_TEXTURE_2D, &value);
    EXPECT_GL_NO_ERROR();
    EXPECT_EQ(value, 1);
}

// Test that we ignore an invalid texture type when EGL_KHR_create_context_no_error is enabled.
TEST_P(ContextNoErrorTest, InvalidTextureType)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    GLTexture texture;
    constexpr GLenum kInvalidTextureType = 0;

    glBindTexture(kInvalidTextureType, texture);
    ASSERT_GL_NO_ERROR();

    glTexParameteri(kInvalidTextureType, GL_TEXTURE_BASE_LEVEL, 0);
    ASSERT_GL_NO_ERROR();
}

// Tests that we can draw with a program that is relinking when GL_KHR_no_error is enabled.
TEST_P(ContextNoErrorTestES3, DrawWithRelinkedProgram)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    int w = getWindowWidth();
    int h = getWindowHeight();
    glViewport(0, 0, w, h);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    constexpr char kVS[] = R"(#version 300 es
void main()
{
    vec2 position = vec2(-1, -1);
    if (gl_VertexID == 1)
        position = vec2(3, -1);
    else if (gl_VertexID == 2)
        position = vec2(-1, 3);
    gl_Position = vec4(position, 0, 1);
})";

    GLuint vs    = CompileShader(GL_VERTEX_SHADER, kVS);
    GLuint red   = CompileShader(GL_FRAGMENT_SHADER, essl3_shaders::fs::Red());
    GLuint bad   = CompileShader(GL_FRAGMENT_SHADER, essl1_shaders::fs::Blue());
    GLuint green = CompileShader(GL_FRAGMENT_SHADER, essl3_shaders::fs::Green());

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, red);
    glLinkProgram(program);

    // Use the program once; it's executable will be installed.
    glUseProgram(program);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w / 4, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Make it fail compilation, the draw should continue to use the old executable
    glDetachShader(program, red);
    glAttachShader(program, bad);
    glLinkProgram(program);

    glScissor(w / 4, 0, w / 2 - w / 4, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Relink the program while it's bound.  It should finish compiling before the following draw is
    // attempted.
    glDetachShader(program, bad);
    glAttachShader(program, green);
    glLinkProgram(program);

    glScissor(w / 2, 0, w - w / 2, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    EXPECT_PIXEL_RECT_EQ(0, 0, w / 2, h, GLColor::red);
    EXPECT_PIXEL_RECT_EQ(w / 2, 0, w - w / 2, h, GLColor::green);
    ASSERT_GL_NO_ERROR();
}

// Tests that a program is resolved before draw calls.
TEST_P(ContextNoErrorTestES3, DrawCommandsWaitOnProgramRelinking)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    int w = getWindowWidth();
    int h = getWindowHeight();
    glViewport(0, 0, w, h);

    glClearColor(0, 0, 0, 1);

    constexpr char kVS[] = R"(#version 300 es
void main()
{
    vec2 position = vec2(-1, -1);
    if (gl_VertexID == 1)
        position = vec2(3, -1);
    else if (gl_VertexID == 2)
        position = vec2(-1, 3);
    gl_Position = vec4(position, 0, 1);
})";

    GLuint vs    = CompileShader(GL_VERTEX_SHADER, kVS);
    GLuint red   = CompileShader(GL_FRAGMENT_SHADER, essl3_shaders::fs::Red());
    GLuint green = CompileShader(GL_FRAGMENT_SHADER, essl3_shaders::fs::Green());

    enum class DrawType
    {
        DrawArrays,
        DrawArraysInstanced,
        MultiDrawArrays
    };

    for (auto drawType :
         {DrawType::DrawArrays, DrawType::DrawArraysInstanced, DrawType::MultiDrawArrays})
    {
        glClear(GL_COLOR_BUFFER_BIT);

        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, red);
        glLinkProgram(program);
        glUseProgram(program);

        // Relink to green while the program is bound.
        glDetachShader(program, red);
        glAttachShader(program, green);
        glLinkProgram(program);

        // Draw must wait until relinking is done.
        std::string command;
        switch (drawType)
        {
            case DrawType::DrawArrays:
                glDrawArrays(GL_TRIANGLES, 0, 3);
                command = "DrawArrays";
                break;
            case DrawType::DrawArraysInstanced:
                glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
                command = "glDrawArraysInstanced";
                break;
            case DrawType::MultiDrawArrays:
                if (IsGLExtensionEnabled("GL_ANGLE_multi_draw"))
                {
                    GLint firsts[1] = {0};
                    GLint counts[1] = {3};
                    glMultiDrawArraysANGLE(GL_TRIANGLES, firsts, counts, 1);
                    command = "MultiDrawArrays";
                }
                break;
        }
        EXPECT_PIXEL_COLOR_EQ(w / 2, h / 2, GLColor::green) << command;
        ASSERT_GL_NO_ERROR() << command;
    }
}

class ContextNoErrorTestES31 : public ContextNoErrorTest
{};

// Tests that a program is resolved before indirect draw calls.
TEST_P(ContextNoErrorTestES31, IndirectDrawCommandsWaitOnProgramRelinking)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_KHR_no_error"));

    int w = getWindowWidth();
    int h = getWindowHeight();
    glViewport(0, 0, w, h);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    constexpr char kVS[] = R"(#version 300 es
void main()
{
    vec2 position = vec2(-1, -1);
    if (gl_VertexID == 1)
        position = vec2(3, -1);
    else if (gl_VertexID == 2)
        position = vec2(-1, 3);
    gl_Position = vec4(position, 0, 1);
})";

    GLuint vs    = CompileShader(GL_VERTEX_SHADER, kVS);
    GLuint red   = CompileShader(GL_FRAGMENT_SHADER, essl3_shaders::fs::Red());
    GLuint green = CompileShader(GL_FRAGMENT_SHADER, essl3_shaders::fs::Green());

    GLVertexArray vao;
    glBindVertexArray(vao);

    const GLushort indexData[3] = {0, 1, 2};
    GLBuffer indexBuffer;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6, indexData, GL_STATIC_DRAW);

    const GLuint drawArraysData[4] = {3, 1, 0, 0};
    GLBuffer drawArraysIndirectCommandBuffer;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawArraysIndirectCommandBuffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, 16, drawArraysData, GL_STATIC_DRAW);

    const GLuint drawElementsData[5] = {3, 1, 0, 0, 0};
    GLBuffer drawElementsIndirectCommandBuffer;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawElementsIndirectCommandBuffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, 20, drawElementsData, GL_STATIC_DRAW);

    enum class DrawType
    {
        DrawArraysIndirect,
        DrawElementsIndirect
    };

    for (auto drawType : {DrawType::DrawArraysIndirect, DrawType::DrawElementsIndirect})
    {
        glClear(GL_COLOR_BUFFER_BIT);

        // Bind buffers before creating the program
        switch (drawType)
        {
            case DrawType::DrawArraysIndirect:
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawArraysIndirectCommandBuffer);
                break;
            case DrawType::DrawElementsIndirect:
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, drawElementsIndirectCommandBuffer);
                break;
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, red);
        glLinkProgram(program);
        glUseProgram(program);

        // Relink to green while the program is bound.
        glDetachShader(program, red);
        glAttachShader(program, green);
        glLinkProgram(program);

        // Draw must wait until relinking is done.
        std::string command;
        switch (drawType)
        {
            case DrawType::DrawArraysIndirect:
                glDrawArraysIndirect(GL_TRIANGLES, nullptr);
                command = "DrawArraysIndirect";
                break;
            case DrawType::DrawElementsIndirect:
                glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT, nullptr);
                command = "DrawElementsIndirect";
                break;
        }
        EXPECT_PIXEL_COLOR_EQ(w / 2, h / 2, GLColor::green) << command;
        ASSERT_GL_NO_ERROR() << command;
    }
}

ANGLE_INSTANTIATE_TEST_ES2_AND_ES3(ContextNoErrorTest);

#define ANGLE_ALL_MULTIDRAW_TEST_PLATFORMS_ES3                                             \
    ES3_D3D11().enable(Feature::AlwaysEnableEmulatedMultidrawExtensions),                  \
        ES3_OPENGL().enable(Feature::AlwaysEnableEmulatedMultidrawExtensions),             \
        ES3_OPENGLES().enable(Feature::AlwaysEnableEmulatedMultidrawExtensions),           \
        ES3_VULKAN().enable(Feature::AlwaysEnableEmulatedMultidrawExtensions),             \
        ES3_VULKAN_SWIFTSHADER().enable(Feature::AlwaysEnableEmulatedMultidrawExtensions), \
        ES3_METAL().enable(Feature::AlwaysEnableEmulatedMultidrawExtensions)
ANGLE_INSTANTIATE_TEST(ContextNoErrorTestES3, ANGLE_ALL_MULTIDRAW_TEST_PLATFORMS_ES3);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ContextNoErrorTestES31);
ANGLE_INSTANTIATE_TEST_ES31(ContextNoErrorTestES31);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ContextNoErrorPPOTest31);
ANGLE_INSTANTIATE_TEST_ES31(ContextNoErrorPPOTest31);
