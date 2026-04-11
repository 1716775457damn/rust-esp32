#ifndef LCD_INTERFACE_H
#define LCD_INTERFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 ST77916 屏幕 (QSPI) 和 LVGL 图形库
 */
void lcd_init_all(void);

/**
 * @brief 将指定缓冲区的数据推送到屏幕
 * @param data RGB565 数据缓冲区
 * @param x, y, w, h 区域信息
 */
void lcd_draw_bitmap(int x, int y, int w, int h, const uint16_t *data);

#ifdef __cplusplus
}
#endif

#endif // LCD_INTERFACE_H
