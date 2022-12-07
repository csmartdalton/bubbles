/*
 * Copyright 2022 Rive
 */

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include <array>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

constexpr static char vs[] = R"(#version 310 es
precision highp float;
uniform vec2 window;
uniform float T;
layout(location=0) in vec3 bubble;
layout(location=1) in vec2 speed;
layout(location=2) in vec4 incolor;
out vec2 coord;
out vec4 color;
void main() {
    vec2 offset = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
    coord = offset;
    color = incolor;
    float r = bubble.z;
    vec2 center = bubble.xy + speed * T;
    vec2 span = window - 2.0 * r;
    center = span - abs(span - mod(center - r, span * 2.0)) + r;
    gl_Position.xy = (center + offset * r) * 2.0 / window - 1.0;
    gl_Position.zw = vec2(0, 1);
})";

constexpr static char fs[] = R"(#version 310 es
precision mediump float;

in vec2 coord;
in vec4 color;

layout(binding=0, r32ui) uniform highp coherent writeonly uimage2D framebuffer;

void main() {
    ivec2 pixelCoord = ivec2(floor(gl_FragCoord.xy));
    float f = coord.x * coord.x + coord.y * coord.y - 1.0;
    float coverage = clamp(.5 - f/fwidth(f), 0.0, 1.0);
    vec4 s = vec4(color.rgb, 1) * (color.a * mix(.25, 1.0, dot(coord, coord)) * coverage);
    imageStore(framebuffer, pixelCoord, uvec4(packUnorm4x8(s)));
})";

static bool compile_and_attach_shader(GLuint program, GLuint type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> infoLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);
        fprintf(stderr, "Failed to compile shader\n");
        int l = 1;
        std::stringstream stream(source);
        std::string lineStr;
        while (std::getline(stream, lineStr, '\n'))
        {
            fprintf(stderr, "%4i| %s\n", l++, lineStr.c_str());
        }
        fprintf(stderr, "%s\n", &infoLog[0]);
        fflush(stderr);
        glDeleteShader(shader);
        return false;
    }
    glAttachShader(program, shader);
    glDeleteShader(shader);
    return true;
}

static bool link_program(GLuint program)
{
    glLinkProgram(program);
    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> infoLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);
        fprintf(stderr, "Failed to link program %s\n", &infoLog[0]);
        fflush(stderr);
        return false;
    }
    return true;
}

static int W = 2048;
static int H = 2048;

struct Bubble
{
    float x, y, r;
    float dx, dy;
    std::array<float, 4> color;
};

static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float frand() { return (float)rand() / RAND_MAX; }
static float frand(float lo, float hi) { return lerp(lo, hi, frand()); }

double now()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(now).time_since_epoch().count() *
           1e-9;
}

int main(int argc, const char* argv[])
{
    // Select the ANGLE backend.
    glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_VULKAN);
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--gl"))
        {
            glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_OPENGL);
        }
        else if (!strcmp(argv[i], "--gles"))
        {
            glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_OPENGLES);
        }
        else if (!strcmp(argv[i], "--d3d"))
        {
            glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_D3D11);
        }
        else if (!strcmp(argv[i], "--vk"))
        {
            glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_VULKAN);
        }
        else if (!strcmp(argv[i], "--mtl"))
        {
            glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_METAL);
        }
    }

    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize glfw.\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    GLFWwindow* window = glfwCreateWindow(W, H, "Rive Bubbles", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        fprintf(stderr, "Failed to create window.\n");
        return -1;
    }

    glfwSetWindowTitle(window, "Rive Bubbles");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    // Load the OpenGL API using glad.
    if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress))
    {
        fprintf(stderr, "Failed to initialize glad.\n");
        return -1;
    }

    printf("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    fflush(stdout);

    GLuint program = glCreateProgram();
    if (!compile_and_attach_shader(program, GL_VERTEX_SHADER, vs) ||
        !compile_and_attach_shader(program, GL_FRAGMENT_SHADER, fs) || !link_program(program))
    {
        return -1;
    }
    glUseProgram(program);
    GLint uniformWindow = glGetUniformLocation(program, "window");
    GLint uniformT = glGetUniformLocation(program, "T");

    // Generate bubbles.
    constexpr int n = 800;
    Bubble bubbles[n];
    for (Bubble& bubble : bubbles)
    {
        float r = lerp(.1f, .3f, powf(frand(), 4));
        bubble.x = (frand(-1 + r, 1 - r) + 1) * 1024.f;
        bubble.y = (frand(-1 + r, 1 - r) + 1) * 1024.f;
        bubble.r = r * 1024.f;
        bubble.dx = (frand() - .5f) * .02f * 1024.f;
        bubble.dy = (frand() - .5f) * .02f * 1024.f;
        // bubble.da = 0; //(frand() - .5) * .03;
        bubble.color = {frand(.5f, 1), frand(.5f, 1), frand(.5f, 1), frand(.75f, 1)};
    }

    GLuint bubbleBuff;
    glGenBuffers(1, &bubbleBuff);
    glBindBuffer(GL_ARRAY_BUFFER, bubbleBuff);
    glBufferData(GL_ARRAY_BUFFER, sizeof(bubbles), bubbles, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Bubble), 0);
    glVertexAttribDivisor(0, 1);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Bubble),
                          reinterpret_cast<const void*>(offsetof(Bubble, dx)));
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          4,
                          GL_FLOAT,
                          GL_TRUE,
                          sizeof(Bubble),
                          reinterpret_cast<const void*>(offsetof(Bubble, color)));
    glVertexAttribDivisor(2, 1);

    GLuint tex = 0;

    GLuint blitFBO;
    glGenFramebuffers(1, &blitFBO);

    GLuint renderFBO;
    glGenFramebuffers(1, &renderFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
    glClearColor(.1f, .1f, .1f, .1f);
    glDisable(GL_DITHER);

    int totalFrames = 0;
    int frames = 0;
    double start = now();
    int lastWidth = 0, lastHeight = 0;

    while (!glfwWindowShouldClose(window))
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (lastWidth != width || lastHeight != height)
        {
            printf("rendering %i bubbles at %i x %i\n", n, width, height);
            glViewport(0, 0, width, height);
            glUniform2f(uniformWindow, static_cast<float>(width), static_cast<float>(height));

            glDeleteTextures(1, &tex);
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);

            glBindFramebuffer(GL_FRAMEBUFFER, blitFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
            glDrawBuffers(0, nullptr);
            glFramebufferParameteri(GL_DRAW_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, width);
            glFramebufferParameteri(GL_DRAW_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, height);
            glBindImageTexture(0, tex, 0, 0, 0, GL_WRITE_ONLY, GL_R32UI);

            lastWidth = width;
            lastHeight = height;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, renderFBO);
        glUniform1f(uniformT, static_cast<float>(totalFrames++));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, blitFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0,
                          0,
                          width,
                          height,
                          0,
                          0,
                          width,
                          height,
                          GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);

        glfwSwapBuffers(window);

        ++frames;
        double end = now();
        double seconds = end - start;
        if (seconds >= 2)
        {
            printf("%f fps\n", frames / seconds);
            fflush(stdout);
            frames = 0;
            start = end;
        }

        glfwPollEvents();
    }

    glfwTerminate();
}
