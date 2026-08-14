/*
 * gfx.h - SDL2 + OpenGLES2 presentation of the C64 framebuffer (C90).
 *
 * The emulated 320x200 RGBA framebuffer is uploaded as a texture each
 * frame and drawn as a letterboxed quad with nearest-neighbor scaling,
 * following the emscripten-sdl2-ogles2 reference patterns.
 */
#ifndef GFX_H
#define GFX_H

int gfx_init(const char *title, int winw, int winh);
void gfx_present(const unsigned char *fb320x200rgba);
void gfx_resize(int w, int h);
void gfx_shutdown(void);

#endif
