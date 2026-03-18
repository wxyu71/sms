/**
 * ui.c — UI 组件层实现
 */

#include "ui.h"

#include <string.h>

/* ================================================================
 * ui_draw_button
 * ================================================================ */
void ui_draw_button(const Button *btn)
{
    /* 1. 外层深色阴影框（2px），营造与背景的层次感 */
    lcd_draw_border(btn->x - 2, btn->y - 2,
                    btn->w + 4, btn->h + 4, 2, COLOR_BG_DARK);

    /* 2. 主色背景 */
    lcd_fill_rect(btn->x, btn->y, btn->w, btn->h, btn->color_bg);

    /* 3. 内侧高亮边框（亮色，模拟发光轮廓） */
    lcd_draw_border(btn->x, btn->y, btn->w, btn->h, 2, btn->color_border);

    /* 4. 文字居中
     *    字符宽 8px，高 16px；双行时总高 38px（16 + 6间距 + 16）
     */
    int len1   = (int)strlen(btn->line1);
    int text_x = btn->x + (btn->w - len1 * 8) / 2;
    int text_y;

    if (btn->line2 != NULL && btn->line2[0] != '\0') {
        /* 双行居中 */
        int len2    = (int)strlen(btn->line2);
        int text_x2 = btn->x + (btn->w - len2 * 8) / 2;
        text_y = btn->y + (btn->h - 38) / 2;
        lcd_draw_string(text_x,  text_y,      btn->line1, COLOR_WHITE,      btn->color_bg);
        lcd_draw_string(text_x2, text_y + 22, btn->line2, COLOR_GRAY_LIGHT, btn->color_bg);
    } else {
        /* 单行居中 */
        text_y = btn->y + (btn->h - 16) / 2;
        lcd_draw_string(text_x, text_y, btn->line1, COLOR_WHITE, btn->color_bg);
    }
}

/* ================================================================
 * ui_hit_test
 * ================================================================ */
const Button *ui_hit_test(const Button *buttons, int count, int x, int y)
{
    for (int i = 0; i < count; i++) {
        const Button *b = &buttons[i];
        if (x >= b->x && x < b->x + b->w &&
            y >= b->y && y < b->y + b->h)
            return b;
    }
    return NULL;
}
