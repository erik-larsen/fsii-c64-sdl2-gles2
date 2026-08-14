/*
 * kbd.c - SDL scancode to C64 keyboard matrix mapping (C90).
 *
 * Matrix convention: col = $DC00 output bit, row = $DC01 input bit.
 * Physical mapping (shift passes through), so shifted characters work
 * as on a real C64 (e.g. '<' is Shift+',' on both).
 */

#include <SDL_scancode.h>
#include "kbd.h"

struct map { int scan, row, col; };

static const struct map keymap[] = {
    { SDL_SCANCODE_BACKSPACE, 0, 0 }, /* INS/DEL */
    { SDL_SCANCODE_RETURN,    1, 0 },
    { SDL_SCANCODE_RIGHT,     2, 0 }, /* CRSR right */
    { SDL_SCANCODE_F7,        3, 0 },
    { SDL_SCANCODE_F1,        4, 0 },
    { SDL_SCANCODE_F3,        5, 0 },
    { SDL_SCANCODE_F5,        6, 0 },
    { SDL_SCANCODE_DOWN,      7, 0 }, /* CRSR down */

    { SDL_SCANCODE_3,         0, 1 },
    { SDL_SCANCODE_W,         1, 1 },
    { SDL_SCANCODE_A,         2, 1 },
    { SDL_SCANCODE_4,         3, 1 },
    { SDL_SCANCODE_Z,         4, 1 },
    { SDL_SCANCODE_S,         5, 1 },
    { SDL_SCANCODE_E,         6, 1 },
    { SDL_SCANCODE_LSHIFT,    7, 1 },

    { SDL_SCANCODE_5,         0, 2 },
    { SDL_SCANCODE_R,         1, 2 },
    { SDL_SCANCODE_D,         2, 2 },
    { SDL_SCANCODE_6,         3, 2 },
    { SDL_SCANCODE_C,         4, 2 },
    { SDL_SCANCODE_F,         5, 2 },
    { SDL_SCANCODE_T,         6, 2 },
    { SDL_SCANCODE_X,         7, 2 },

    { SDL_SCANCODE_7,         0, 3 },
    { SDL_SCANCODE_Y,         1, 3 },
    { SDL_SCANCODE_G,         2, 3 },
    { SDL_SCANCODE_8,         3, 3 },
    { SDL_SCANCODE_B,         4, 3 },
    { SDL_SCANCODE_H,         5, 3 },
    { SDL_SCANCODE_U,         6, 3 },
    { SDL_SCANCODE_V,         7, 3 },

    { SDL_SCANCODE_9,         0, 4 },
    { SDL_SCANCODE_I,         1, 4 },
    { SDL_SCANCODE_J,         2, 4 },
    { SDL_SCANCODE_0,         3, 4 },
    { SDL_SCANCODE_M,         4, 4 },
    { SDL_SCANCODE_K,         5, 4 },
    { SDL_SCANCODE_O,         6, 4 },
    { SDL_SCANCODE_N,         7, 4 },

    { SDL_SCANCODE_KP_PLUS,   0, 5 }, /* + */
    { SDL_SCANCODE_EQUALS,    0, 5 }, /* convenience: '=' key as '+' */
    { SDL_SCANCODE_P,         1, 5 },
    { SDL_SCANCODE_L,         2, 5 },
    { SDL_SCANCODE_MINUS,     3, 5 },
    { SDL_SCANCODE_PERIOD,    4, 5 },
    { SDL_SCANCODE_SEMICOLON, 5, 5 }, /* ':' position */
    { SDL_SCANCODE_LEFTBRACKET, 6, 5 }, /* '@' position */
    { SDL_SCANCODE_COMMA,     7, 5 },

    { SDL_SCANCODE_INSERT,    0, 6 }, /* pound */
    { SDL_SCANCODE_KP_MULTIPLY, 1, 6 },
    { SDL_SCANCODE_APOSTROPHE, 2, 6 }, /* ';' position */
    { SDL_SCANCODE_HOME,      3, 6 },
    { SDL_SCANCODE_RSHIFT,    4, 6 },
    { SDL_SCANCODE_RIGHTBRACKET, 5, 6 }, /* '=' position */
    { SDL_SCANCODE_GRAVE,     6, 6 }, /* up-arrow position */
    { SDL_SCANCODE_SLASH,     7, 6 },

    { SDL_SCANCODE_1,         0, 7 },
    { SDL_SCANCODE_TAB,       1, 7 }, /* left-arrow key */
    { SDL_SCANCODE_LCTRL,     2, 7 },
    { SDL_SCANCODE_RCTRL,     2, 7 },
    { SDL_SCANCODE_2,         3, 7 },
    { SDL_SCANCODE_SPACE,     4, 7 },
    { SDL_SCANCODE_LALT,      5, 7 }, /* Commodore key */
    { SDL_SCANCODE_Q,         6, 7 },
    { SDL_SCANCODE_ESCAPE,    7, 7 }  /* RUN/STOP */
};

int kbd_map(int sdl_scancode, int *row, int *col)
{
    unsigned i;
    for (i = 0; i < sizeof keymap / sizeof keymap[0]; i++) {
        if (keymap[i].scan == sdl_scancode) {
            *row = keymap[i].row;
            *col = keymap[i].col;
            return 1;
        }
    }
    return 0;
}
