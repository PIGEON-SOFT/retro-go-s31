#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#define RGB_BUFFER_COUNT 4
#define RGB_BYTES_PER_PIXEL 2
#define RGB_VIEW_LEFT   RG_TOUCH_GAMEPAD_VIEW_LEFT
#define RGB_VIEW_TOP    RG_TOUCH_GAMEPAD_VIEW_TOP
#define RGB_VIEW_WIDTH  RG_TOUCH_GAMEPAD_VIEW_WIDTH
#define RGB_VIEW_HEIGHT RG_TOUCH_GAMEPAD_VIEW_HEIGHT
#define RGB_CONTROLS_TOP 340

static esp_lcd_panel_handle_t rgb_panel;
static QueueHandle_t rgb_buffers;
static int rgb_window_left;
static int rgb_window_top;
static int rgb_window_width;
static int rgb_window_sent;
static bool rgb_controls_dirty = true;
static uint16_t *rgb_controls_buffer;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

static inline bool rgb_in_rect(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

static bool rgb_in_circle(int x, int y, int cx, int cy, int r)
{
    int dx = x - cx;
    int dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static uint8_t rgb_font_row(char ch, int row)
{
    static const uint8_t blank[7] = {0};
    static const uint8_t font_a[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t font_b[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    static const uint8_t font_c[7] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    static const uint8_t font_d[7] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    static const uint8_t font_e[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t font_l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t font_m[7] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t font_n[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t font_o[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t font_p[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t font_r[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t font_s[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t font_t[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t font_u[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t font_w[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
    const uint8_t *glyph = blank;

    switch (ch) {
    case 'A': glyph = font_a; break;
    case 'B': glyph = font_b; break;
    case 'C': glyph = font_c; break;
    case 'D': glyph = font_d; break;
    case 'E': glyph = font_e; break;
    case 'L': glyph = font_l; break;
    case 'M': glyph = font_m; break;
    case 'N': glyph = font_n; break;
    case 'O': glyph = font_o; break;
    case 'P': glyph = font_p; break;
    case 'R': glyph = font_r; break;
    case 'S': glyph = font_s; break;
    case 'T': glyph = font_t; break;
    case 'U': glyph = font_u; break;
    case 'W': glyph = font_w; break;
    default: break;
    }
    return glyph[row];
}

static bool rgb_text_pixel(int x, int y, int left, int top, const char *text, int scale)
{
    if (y < top || y >= top + 7 * scale || x < left) {
        return false;
    }

    int local_y = (y - top) / scale;
    int char_pitch = 6 * scale;
    int local_x = x - left;
    int index = local_x / char_pitch;
    int col = (local_x % char_pitch) / scale;

    if (col >= 5) {
        return false;
    }
    for (int i = 0; i < index; ++i) {
        if (!text[i]) {
            return false;
        }
    }
    char ch = text[index];
    if (!ch) {
        return false;
    }
    return (rgb_font_row(ch, local_y) & (0x10 >> col)) != 0;
}

static bool rgb_label_pixel(int x, int y)
{
    return rgb_text_pixel(x, y, 710, 377, "A", 3) ||
           rgb_text_pixel(x, y, 620, 423, "B", 3) ||
           rgb_text_pixel(x, y, 238, 371, "L", 2) ||
           rgb_text_pixel(x, y, 548, 371, "R", 2) ||
           rgb_text_pixel(x, y, 309, 441, "SELECT", 2) ||
           rgb_text_pixel(x, y, 425, 441, "START", 2) ||
           rgb_text_pixel(x, y, 328, 371, "MENU", 2) ||
           rgb_text_pixel(x, y, 430, 371, "OPT", 2) ||
           rgb_text_pixel(x, y, 102, 360, "U", 2) ||
           rgb_text_pixel(x, y, 102, 440, "D", 2) ||
           rgb_text_pixel(x, y, 50, 402, "L", 2) ||
           rgb_text_pixel(x, y, 154, 402, "R", 2);
}

static uint16_t rgb_control_pixel(int x, int y)
{
    const uint16_t bg = rgb565(16, 18, 22);
    const uint16_t outline = rgb565(115, 122, 134);
    const uint16_t fill = rgb565(38, 45, 56);
    const uint16_t accent = rgb565(82, 164, 255);
    const uint16_t danger = rgb565(255, 105, 80);
    const uint16_t muted = rgb565(70, 78, 90);
    const uint16_t text = rgb565(245, 248, 252);

    if (y < RGB_CONTROLS_TOP) {
        return bg;
    }

    if (rgb_in_rect(x, y, 0, RGB_CONTROLS_TOP, RG_SCREEN_WIDTH, 2)) {
        return outline;
    }

    if (rgb_label_pixel(x, y)) {
        return text;
    }

    bool dpad =
        rgb_in_rect(x, y, 80, 342, 55, 136) ||
        rgb_in_rect(x, y, 25, 382, 165, 60);
    bool dpad_core =
        rgb_in_rect(x, y, 93, 355, 29, 110) ||
        rgb_in_rect(x, y, 38, 395, 139, 34);
    if (dpad) {
        return dpad_core ? fill : outline;
    }

    if (rgb_in_circle(x, y, 720, 387, 45)) {
        return rgb_in_circle(x, y, 720, 387, 38) ? accent : outline;
    }
    if (rgb_in_circle(x, y, 630, 433, 45)) {
        return rgb_in_circle(x, y, 630, 433, 38) ? danger : outline;
    }

    if (rgb_in_rect(x, y, 205, 352, 80, 58) ||
        rgb_in_rect(x, y, 315, 352, 75, 58) ||
        rgb_in_rect(x, y, 410, 352, 75, 58) ||
        rgb_in_rect(x, y, 515, 352, 80, 58) ||
        rgb_in_rect(x, y, 300, 422, 90, 56) ||
        rgb_in_rect(x, y, 410, 422, 90, 56)) {
        return muted;
    }

    return bg;
}

static void rgb_lcd_draw_control_rect(uint16_t *buffer, int left, int top, int width, int height)
{
    const int lines_per_chunk = 16;

    for (int y = top; y < top + height; y += lines_per_chunk) {
        int lines = RG_MIN(lines_per_chunk, top + height - y);
        for (int row = 0; row < lines; ++row) {
            for (int x = 0; x < width; ++x) {
                buffer[row * width + x] = rgb_control_pixel(left + x, y + row);
            }
        }
        esp_lcd_panel_draw_bitmap(rgb_panel, left, y, left + width, y + lines, buffer);
    }
}

static void rgb_lcd_draw_controls(void)
{
    const int lines_per_chunk = 16;
    if (!rgb_controls_buffer) {
        rgb_controls_buffer = rg_alloc(RG_SCREEN_WIDTH * lines_per_chunk * RGB_BYTES_PER_PIXEL, MEM_DMA);
    }
    if (!rgb_controls_buffer) {
        return;
    }

    rgb_lcd_draw_control_rect(rgb_controls_buffer, 0, RGB_CONTROLS_TOP, RG_SCREEN_WIDTH,
                              RG_SCREEN_HEIGHT - RGB_CONTROLS_TOP);
    rgb_controls_dirty = false;
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    uint16_t *buffer = NULL;
    if (length > LCD_BUFFER_LENGTH) {
        RG_LOGW("RGB LCD buffer request too large: %u", (unsigned)length);
    }
    if (xQueueReceive(rgb_buffers, &buffer, pdMS_TO_TICKS(2500)) != pdTRUE) {
        RG_PANIC("display");
    }
    return buffer;
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    if (length > 0) {
        for (size_t i = 0; i < length; ++i) {
            buffer[i] = (buffer[i] << 8) | (buffer[i] >> 8);
        }

        for (size_t offset = 0; offset < length;) {
            int row = rgb_window_sent / rgb_window_width;
            int col = rgb_window_sent % rgb_window_width;
            int available = rgb_window_width - col;
            int pixels = RG_MIN((int)(length - offset), available);

            if (col == 0) {
                int full_lines = (length - offset) / rgb_window_width;
                if (full_lines > 0) {
                    pixels = full_lines * rgb_window_width;
                    esp_lcd_panel_draw_bitmap(rgb_panel,
                                              rgb_window_left,
                                              rgb_window_top + row,
                                              rgb_window_left + rgb_window_width,
                                              rgb_window_top + row + full_lines,
                                              buffer + offset);
                    rgb_window_sent += pixels;
                    offset += pixels;
                    continue;
                }
            }

            esp_lcd_panel_draw_bitmap(rgb_panel,
                                      rgb_window_left + col,
                                      rgb_window_top + row,
                                      rgb_window_left + col + pixels,
                                      rgb_window_top + row + 1,
                                      buffer + offset);
            rgb_window_sent += pixels;
            offset += pixels;
        }
    }
    xQueueSend(rgb_buffers, &buffer, portMAX_DELAY);
}

static void lcd_set_window(int left, int top, int width, int height)
{
    rgb_window_left = left;
    rgb_window_top = top;
    rgb_window_width = width;
    rgb_window_sent = 0;

    if (top + height > RGB_CONTROLS_TOP) {
        rgb_controls_dirty = true;
    }
}

static void lcd_sync(void)
{
    if (rgb_controls_dirty) {
        rgb_lcd_draw_controls();
    }
}

static void lcd_set_backlight(float percent)
{
    (void)percent;
    rgb_controls_dirty = true;
    lcd_sync();
}

static void lcd_init(void)
{
    rgb_buffers = xQueueCreate(RGB_BUFFER_COUNT, sizeof(uint16_t *));
    RG_ASSERT(rgb_buffers, "RGB LCD buffer queue allocation failed.");

    while (uxQueueSpacesAvailable(rgb_buffers)) {
        void *buffer = rg_alloc(LCD_BUFFER_LENGTH * RGB_BYTES_PER_PIXEL, MEM_DMA);
        RG_ASSERT(buffer, "RGB LCD buffer allocation failed.");
        xQueueSend(rgb_buffers, &buffer, portMAX_DELAY);
    }

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .dma_burst_size = 64,
        .disp_gpio_num = RG_GPIO_LCD_DISP_EN,
        .pclk_gpio_num = RG_GPIO_LCD_PCLK,
        .vsync_gpio_num = RG_GPIO_LCD_VSYNC,
        .hsync_gpio_num = RG_GPIO_LCD_HSYNC,
        .de_gpio_num = RG_GPIO_LCD_DE,
        .data_gpio_nums = {
            RG_GPIO_LCD_DATA0, RG_GPIO_LCD_DATA1, RG_GPIO_LCD_DATA2, RG_GPIO_LCD_DATA3,
            RG_GPIO_LCD_DATA4, RG_GPIO_LCD_DATA5, RG_GPIO_LCD_DATA6, RG_GPIO_LCD_DATA7,
            RG_GPIO_LCD_DATA8, RG_GPIO_LCD_DATA9, RG_GPIO_LCD_DATA10, RG_GPIO_LCD_DATA11,
            RG_GPIO_LCD_DATA12, RG_GPIO_LCD_DATA13, RG_GPIO_LCD_DATA14, RG_GPIO_LCD_DATA15,
        },
        .timings = {
            .pclk_hz = RG_RGB_LCD_PIXEL_CLOCK_HZ,
            .h_res = RG_SCREEN_WIDTH,
            .v_res = RG_SCREEN_HEIGHT,
            .hsync_pulse_width = RG_RGB_LCD_HSYNC,
            .hsync_back_porch = RG_RGB_LCD_HBP,
            .hsync_front_porch = RG_RGB_LCD_HFP,
            .vsync_pulse_width = RG_RGB_LCD_VSYNC,
            .vsync_back_porch = RG_RGB_LCD_VBP,
            .vsync_front_porch = RG_RGB_LCD_VFP,
        },
        .flags.fb_in_psram = true,
    };

    RG_ASSERT(esp_lcd_new_rgb_panel(&panel_config, &rgb_panel) == ESP_OK, "RGB LCD panel allocation failed.");
    RG_ASSERT(esp_lcd_panel_reset(rgb_panel) == ESP_OK, "RGB LCD reset failed.");
    RG_ASSERT(esp_lcd_panel_init(rgb_panel) == ESP_OK, "RGB LCD init failed.");
}

static void lcd_deinit(void)
{
    if (rgb_panel) {
        esp_lcd_panel_del(rgb_panel);
        rgb_panel = NULL;
    }
}

const rg_display_driver_t rg_display_driver_rgb_lcd = {
    .name = "rgb_lcd",
};
