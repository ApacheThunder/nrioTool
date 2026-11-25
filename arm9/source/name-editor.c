#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "button-legend.h"
#include "file-transfer.h"
#include "name-editor.h"
#include "ui.h"

enum { KEY_TOP = 3, KEY_ROWS = 6, PREVIEW_WIDTH = 30 };
enum edit_key { EDIT_SPACE = 256, EDIT_ERASE, EDIT_CASE, EDIT_SYMBOLS, EDIT_SAVE, EDIT_CANCEL };
struct key { unsigned x; const char *label; enum edit_key code; };
struct layout { const char *text; unsigned left; };

static const struct layout letters[] = {
    {"1234567890", 1}, {"qwertyuiop", 1}, {"asdfghjkl", 2}, {"zxcvbnm-_.", 1},
};
static const struct layout symbols[] = {
    {"!@#$%^&()+", 1}, {"-_=~`[]{}'", 1}, {",.;", 11}, {"0123456789", 1},
};
static const struct key editing[] = {
    {1, "Space", EDIT_SPACE}, {9, "Del", EDIT_ERASE}, {15, "Case", EDIT_CASE},
    {22, "Sym", EDIT_SYMBOLS},
};
static const struct key commands[] = {
    {1, "Save", EDIT_SAVE}, {9, "Cancel", EDIT_CANCEL},
};

static unsigned key_count(unsigned row, bool symbol)
{
    if (row < 4) return strlen((symbol ? symbols : letters)[row].text);
    return row == 4 ? sizeof(editing) / sizeof(*editing) : sizeof(commands) / sizeof(*commands);
}

static unsigned key_code(unsigned row, unsigned col, bool symbol, bool upper)
{
    if (row >= 4) return (row == 4 ? editing : commands)[col].code;
    unsigned c = (symbol ? symbols : letters)[row].text[col];
    return upper && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static unsigned key_start(unsigned row, unsigned col, bool symbol)
{
    if (row >= 4) return (row == 4 ? editing : commands)[col].x;
    return (symbol ? symbols : letters)[row].left + 3 * col;
}

static unsigned key_width(unsigned row, unsigned col)
{
    return row < 4 ? 3 : strlen((row == 4 ? editing : commands)[col].label) + 2;
}

static void draw_keys(unsigned row, unsigned selected, bool focus, bool symbol, bool upper)
{
    unsigned screen_col = 0;
    for (unsigned i = 0; i < key_count(row, symbol); ++i) {
        unsigned x = key_start(row, i, symbol);
        const bool active = focus && selected == i;
        while (screen_col < x) { putchar(' '); ++screen_col; }
        if (row < 4) printf(active ? ">%c<" : "[%c]", key_code(row, i, symbol, upper));
        else printf(active ? ">%s<" : "[%s]", (row == 4 ? editing : commands)[i].label);
        screen_col += key_width(row, i);
    }
    putchar('\n');
    if (row + 1 < KEY_ROWS) putchar('\n');
}

static unsigned nearest(unsigned target, unsigned row, bool symbol)
{
    unsigned best = 0, nearest_distance = 32;
    for (unsigned i = 0; i < key_count(row, symbol); ++i) {
        unsigned centre = key_start(row, i, symbol) + key_width(row, i) / 2;
        unsigned distance = target > centre ? target - centre : centre - target;
        if (distance < nearest_distance) { best = i; nearest_distance = distance; }
    }
    return best;
}

bool nrio_edit_name(const char *title, char name[256])
{
    unsigned row = 1, col = 0, len = strlen(name), cursor = len, offset = 0;
    const int console_top = BottomConsoleRow();
    bool upper = false, symbol = false, invalid = false;
    for (;;) {
        if (cursor < offset) offset = cursor;
        if (cursor >= offset + PREVIEW_WIDTH) offset = cursor - PREVIEW_WIDTH + 1;
        consoleClear();
        puts(invalid ? "Enter a valid FAT name" : title);
        putchar(offset ? '<' : ' ');
        for (unsigned i = offset; i < len && i < offset + PREVIEW_WIDTH; ++i) {
            unsigned char c = name[i];
            putchar(c >= 32 && c < 127 ? c : '?');
        }
        putchar('\n');
        printf("%*s^\n", (int)(cursor - offset + 1), "");
        for (unsigned i = 0; i < KEY_ROWS; ++i) draw_keys(i, col, i == row, symbol, upper);
        nrio_button_legend("Pad+A  L/R Cursor  B Back", "X Del Y Case SEL Sym ST OK");

        unsigned buttons;
        do {
            swiWaitForVBlank();
            scanKeys();
            buttons = keysDown();
        } while (!buttons);

        unsigned code = 0;
        if (buttons & KEY_TOUCH) {
            touchPosition touch;
            touchRead(&touch);
            int x = touch.px / 8, y = touch.py / 8 - console_top;
            if (y == 1 || y == 2) {
                if (x == 0 && offset) cursor = offset - 1;
                else cursor = offset + (x > 0 ? x - 1 : 0);
                if (cursor > len) cursor = len;
            } else if (y >= KEY_TOP && y < KEY_TOP + KEY_ROWS * 2 - 1) {
                unsigned target = (y - KEY_TOP) / 2;
                for (unsigned i = 0; i < key_count(target, symbol); ++i) {
                    unsigned start = key_start(target, i, symbol);
                    if ((unsigned)x >= start && (unsigned)x < start + key_width(target, i)) {
                        row = target;
                        col = i;
                        code = key_code(row, col, symbol, upper);
                        break;
                    }
                }
            }
        } else if (buttons & KEY_B) code = EDIT_CANCEL;
        else if (buttons & KEY_START) code = EDIT_SAVE;
        else if (buttons & KEY_X) code = EDIT_ERASE;
        else if (buttons & KEY_Y) code = EDIT_CASE;
        else if (buttons & KEY_SELECT) code = EDIT_SYMBOLS;
        else if (buttons & KEY_L) { if (cursor) --cursor; }
        else if (buttons & KEY_R) { if (cursor < len) ++cursor; }
        else if (buttons & KEY_A) code = key_code(row, col, symbol, upper);
        else if (buttons & (KEY_UP | KEY_DOWN)) {
            unsigned centre = key_start(row, col, symbol) + key_width(row, col) / 2;
            row = (row + (buttons & KEY_UP ? KEY_ROWS - 1 : 1)) % KEY_ROWS;
            col = nearest(centre, row, symbol);
        } else if (buttons & (KEY_LEFT | KEY_RIGHT)) {
            unsigned width = key_count(row, symbol);
            col = (col + (buttons & KEY_LEFT ? width - 1 : 1)) % width;
        }

        if (!code) continue;
        invalid = false;
        switch (code) {
            case EDIT_CANCEL: return false;
            case EDIT_SAVE:
                if (nrio_file_valid_name(name)) return true;
                invalid = true;
                break;
            case EDIT_SYMBOLS:
                symbol = !symbol;
                if (col >= key_count(row, symbol)) col = key_count(row, symbol) - 1;
                break;
            case EDIT_CASE: upper = !upper; break;
            case EDIT_ERASE:
                if (cursor) {
                    memmove(name + cursor - 1, name + cursor, len - cursor + 1);
                    --cursor;
                    --len;
                }
                break;
            default:
                if (len < 255) {
                    memmove(name + cursor + 1, name + cursor, len - cursor + 1);
                    name[cursor++] = code == EDIT_SPACE ? ' ' : (char)code;
                    ++len;
                }
                break;
        }
    }
}
