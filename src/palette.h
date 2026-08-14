/*
 * palette.h - C64 16-color palette as RGBA (colodore-derived), C90.
 */
#ifndef PALETTE_H
#define PALETTE_H

/* R,G,B,A per index 0..15 */
static const unsigned char c64_palette[16][4] = {
    {   0,   0,   0, 255 }, /* 0 black       */
    { 255, 255, 255, 255 }, /* 1 white       */
    { 129,  51,  47, 255 }, /* 2 red         */
    { 117, 206, 200, 255 }, /* 3 cyan        */
    { 142,  60, 151, 255 }, /* 4 purple      */
    {  86, 172,  77, 255 }, /* 5 green       */
    {  46,  44, 155, 255 }, /* 6 blue        */
    { 237, 241, 113, 255 }, /* 7 yellow      */
    { 142,  80,  41, 255 }, /* 8 orange      */
    {  85,  56,   0, 255 }, /* 9 brown       */
    { 196, 108, 104, 255 }, /* 10 light red  */
    {  74,  74,  74, 255 }, /* 11 dark grey  */
    { 123, 123, 123, 255 }, /* 12 grey       */
    { 169, 255, 159, 255 }, /* 13 light green*/
    { 112, 109, 235, 255 }, /* 14 light blue */
    { 178, 178, 178, 255 }  /* 15 light grey */
};

#endif
