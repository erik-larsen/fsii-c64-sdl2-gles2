/*
 * gfx.c - SDL2 + OpenGLES2 presentation layer (C90).
 *
 * Emscripten builds use a real GLES2 context (FULL_ES2); the macOS
 * build uses the OpenGL 2.1 compatibility subset, which accepts the
 * same buffers/shaders API.  Shader sources carry a GL_ES precision
 * guard so one source works on both.
 */

#include <stdio.h>
#include <string.h>
#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <SDL_opengles2.h>
#else
#define GL_SILENCE_DEPRECATION 1
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#endif

#include "gfx.h"

#define FB_W 320
#define FB_H 200

static SDL_Window *win;
static SDL_GLContext ctx;
static GLuint prog, vbo, tex;
static GLint attr_pos, attr_uv, uni_tex;
static int view_w, view_h;

static const char *vs_src =
    "attribute vec2 pos;\n"
    "attribute vec2 uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = uv;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *fs_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(tex, v_uv);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "shader: %s\n", log);
        return 0;
    }
    return s;
}

int gfx_init(const char *title, int winw, int winh)
{
    GLuint vs, fs;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, winw, winh,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                           SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(1);

    vs = compile(GL_VERTEX_SHADER, vs_src);
    fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return -1;
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glBindAttribLocation(prog, 1, "uv");
    glLinkProgram(prog);
    glUseProgram(prog);
    attr_pos = 0;
    attr_uv = 1;
    uni_tex = glGetUniformLocation(prog, "tex");
    glUniform1i(uni_tex, 0);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FB_W, FB_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);

    SDL_GL_GetDrawableSize(win, &view_w, &view_h);
    return 0;
}

void gfx_resize(int w, int h)
{
    (void)w;
    (void)h;
    SDL_GL_GetDrawableSize(win, &view_w, &view_h);
}

void gfx_present(const unsigned char *fb)
{
    /* letterbox to 4:3 (the C64 pixel aspect on period displays) */
    float sx = 1.0f, sy = 1.0f;
    float want = 4.0f / 3.0f;
    float have = (float)view_w / (float)view_h;
    GLfloat verts[16];

    if (have > want)
        sx = want / have;
    else
        sy = have / want;

    /* x, y, u, v */
    verts[0] = -sx; verts[1] = -sy; verts[2] = 0.0f; verts[3] = 1.0f;
    verts[4] =  sx; verts[5] = -sy; verts[6] = 1.0f; verts[7] = 1.0f;
    verts[8] = -sx; verts[9] =  sy; verts[10] = 0.0f; verts[11] = 0.0f;
    verts[12] = sx; verts[13] =  sy; verts[14] = 1.0f; verts[15] = 0.0f;

    glViewport(0, 0, view_w, view_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB_W, FB_H, GL_RGBA,
                    GL_UNSIGNED_BYTE, fb);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)attr_pos);
    glVertexAttribPointer((GLuint)attr_pos, 2, GL_FLOAT, GL_FALSE,
                          4 * (GLsizei)sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray((GLuint)attr_uv);
    glVertexAttribPointer((GLuint)attr_uv, 2, GL_FLOAT, GL_FALSE,
                          4 * (GLsizei)sizeof(GLfloat),
                          (void *)(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    SDL_GL_SwapWindow(win);
}

void gfx_set_title(const char *title)
{
    if (win)
        SDL_SetWindowTitle(win, title);
}

void gfx_shutdown(void)
{
    if (ctx)
        SDL_GL_DeleteContext(ctx);
    if (win)
        SDL_DestroyWindow(win);
    SDL_Quit();
}
