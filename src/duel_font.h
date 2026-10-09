/* XWADuelVR M7 stroke font: CAPS-ASCII line glyphs for VR menu panels.
 *
 * No assets, no licensing issues, always crisp: glyphs are segment lists
 * in a 5x7 box (x 0..4 left->right, y 0..6 bottom->top) drawn through the
 * existing dyn-line pipe. Lowercase folds to uppercase; unknown chars
 * (incl. lowercase accents) render blank. Menu strings MUST use the
 * covered set; test_menu_strings enforces it against the live menu rows.
 */
#ifndef XWADUELVR_FONT_H
#define XWADUELVR_FONT_H

#define DUEL_FONT_W 5.0f
#define DUEL_FONT_H 7.0f
#define DUEL_FONT_ADV 6.0f /* 5 + 1 spacing */
/* Max segments of any covered glyph (B/D/G/S/0/2/3/5/6/8 = 5-6). */
#define DUEL_FONT_MAXSEGS 6

typedef struct DuelFontSeg {
    int x0, y0, x1, y1;
} DuelFontSeg;

/* Segments for one glyph (0 = space/unknown: nothing to draw). */
int DuelFont_Glyph(char c, DuelFontSeg *out, int cap);
/* Advance width of a NUL-terminated string in box units. */
float DuelFont_TextW(const char *s);

#endif /* XWADUELVR_FONT_H */
