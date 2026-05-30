#include "rg_system.h"
#include "rg_display.h"

#include <stdlib.h>
#include <string.h>

#define LCD_BUFFER_LENGTH (RG_SCREEN_WIDTH * 4) // In pixels

// static rg_display_driver_t driver;
static rg_task_t *display_task_queue;
static rg_display_counters_t counters;
static rg_display_config_t config;
// static rg_surface_t *osd;
static rg_surface_t *border;
static rg_display_t display;
static int16_t map_viewport_to_source_x[RG_SCREEN_WIDTH + 1];
static int16_t map_viewport_to_source_y[RG_SCREEN_HEIGHT + 1];
static uint32_t screen_line_checksum[RG_SCREEN_HEIGHT + 1];

#define LINE_IS_REPEATED(Y) (map_viewport_to_source_y[(Y)] == map_viewport_to_source_y[(Y) - 1])
// This is to avoid flooring a number that is approximated to .9999999 and be explicit about it
#define FLOAT_TO_INT(x) ((int)((x) + 0.1f))

static const char *SETTING_BACKLIGHT = "DispBacklight";
static const char *SETTING_SCALING = "DispScaling";
static const char *SETTING_FILTER = "DispFilter";
static const char *SETTING_ROTATION = "DispRotation";
static const char *SETTING_BORDER = "DispBorder";
static const char *SETTING_CUSTOM_ZOOM = "DispCustomZoom";
static const char *SETTING_SHOW_FPS = "DispShowFPS";

static void lcd_init(void);
static void lcd_deinit(void);
static void lcd_sync(void);
static void lcd_set_rotation(int rotation);
static void lcd_set_backlight(float percent);
static void lcd_set_window(int left, int top, int width, int height);
static inline uint16_t *lcd_get_buffer(size_t length);
static inline void lcd_send_buffer(uint16_t *buffer, size_t length);
void rg_display_clear_rect(int left, int top, int width, int height, uint16_t color_le);

static const uint8_t overlay_font[][5] = {
    ['0' - ' '] = {0x3E,0x51,0x49,0x45,0x3E}, ['1' - ' '] = {0x00,0x42,0x7F,0x40,0x00},
    ['2' - ' '] = {0x42,0x61,0x51,0x49,0x46}, ['3' - ' '] = {0x21,0x41,0x45,0x4B,0x31},
    ['4' - ' '] = {0x18,0x14,0x12,0x7F,0x10}, ['5' - ' '] = {0x27,0x45,0x45,0x45,0x39},
    ['6' - ' '] = {0x3C,0x4A,0x49,0x49,0x30}, ['7' - ' '] = {0x01,0x71,0x09,0x05,0x03},
    ['8' - ' '] = {0x36,0x49,0x49,0x49,0x36}, ['9' - ' '] = {0x06,0x49,0x49,0x29,0x1E},
    ['%' - ' '] = {0x23,0x13,0x08,0x64,0x62}, [' ' - ' '] = {0x00,0x00,0x00,0x00,0x00},
    ['B' - ' '] = {0x7F,0x49,0x49,0x49,0x36}, ['F' - ' '] = {0x7F,0x09,0x09,0x09,0x01},
    ['P' - ' '] = {0x7F,0x09,0x09,0x09,0x06}, ['S' - ' '] = {0x46,0x49,0x49,0x49,0x31},
    ['Y' - ' '] = {0x07,0x08,0x70,0x08,0x07}, [':' - ' '] = {0x00,0x36,0x36,0x00,0x00},
};

static void draw_fps_overlay(void)
{
    if (!config.show_fps)
        return;

    const rg_app_t *app = rg_system_get_app();
    if (!app || !app->initialized || app->isLauncher)
        return;

    rg_stats_t stats = rg_system_get_counters();
    char text[32];
    snprintf(text, sizeof(text), "%02d FPS B:%02d%%", (int)(stats.totalFPS + 0.5f), (int)(stats.busyPercent + 0.5f));

    const int scale = 2;
    const int char_w = 6 * scale;
    const int width = strlen(text) * char_w + 4;
    const int height = 7 * scale + 4;
    uint16_t *buf = lcd_get_buffer(width * height);
    if (!buf)
        return;

    for (int i = 0; i < width * height; ++i)
        buf[i] = C_BLACK;

    for (int c = 0; text[c]; ++c)
    {
        char ch = text[c];
        if (ch < ' ' || ch > 'Y')
            ch = ' ';
        const uint8_t *glyph = overlay_font[ch - ' '];
        for (int x = 0; x < 5; ++x)
        {
            for (int y = 0; y < 7; ++y)
            {
                if (!(glyph[x] & (1 << y)))
                    continue;
                int px = 2 + c * char_w + x * scale;
                int py = 2 + y * scale;
                for (int yy = 0; yy < scale; ++yy)
                    for (int xx = 0; xx < scale; ++xx)
                        buf[(py + yy) * width + px + xx] = C_WHITE;
            }
        }
    }

    lcd_set_window(display.screen.margins.left + 4, display.screen.margins.top + 4, width, height);
    lcd_send_buffer(buf, width * height);
}

#if RG_SCREEN_DRIVER == 0 || RG_SCREEN_DRIVER == 1 /* ILI9341/ST7789 */
#include "drivers/display/ili9341.h"
#elif RG_SCREEN_DRIVER == 2
#include "drivers/display/rgb_lcd.h"
#elif RG_SCREEN_DRIVER == 99
#include "drivers/display/sdl2.h"
#else
#include "drivers/display/dummy.h"
#endif

static inline unsigned blend_pixels(unsigned a, unsigned b)
{
    // Fast path (taken 80-90% of the time)
    if (a == b)
        return a;

    // Not the original author, but a good explanation is found at:
    // https://medium.com/@luc.trudeau/fast-averaging-of-high-color-16-bit-pixels-cb4ac7fd1488
    a = (a << 8) | (a >> 8);
    b = (b << 8) | (b >> 8);
    unsigned s = a ^ b;
    unsigned v = ((s & 0xF7DEU) >> 1) + (a & b) + (s & 0x0821U);
    return (v << 8) | (v >> 8);

    // This is my attempt at averaging two 565BE values without swapping bytes (3x the speed of the code above)
    // return (((a ^ b) & 0b1101111011110110U) >> 1) + (a & b);
}

#if defined(RG_TOUCH_GAMEPAD_FRAME) && RG_TOUCH_GAMEPAD_FRAME
static bool use_touch_gamepad_frame(void)
{
    return true;
}
#endif

static inline void write_update(const rg_surface_t *update)
{
    const int64_t time_start = rg_system_timer();

    bool filter_x = display.viewport.filter_x;
    bool filter_y = display.viewport.filter_y;
    int draw_left = display.viewport.left;
    int draw_top = display.viewport.top;
    int draw_width = display.viewport.width;
    int draw_height = display.viewport.height;

    int crop_left = 0;
    int crop_top = 0;

    if (draw_left < 0)
    {
        crop_left += -draw_left * display.viewport.step_x;
        draw_width += draw_left * 2;
        draw_left = 0;
    }

    if (draw_top < 0)
    {
        crop_top += -draw_top * display.viewport.step_y;
        draw_height += draw_top * 2;
        draw_top = 0;
    }

    const int format = update->format;
    const int stride = update->stride;
    const void *data = update->data + update->offset + (crop_top * stride) + (crop_left * RG_PIXEL_GET_SIZE(format));
    const uint16_t *palette = update->palette;

    const bool partial_update = RG_SCREEN_PARTIAL_UPDATES;

    int lines_per_buffer = LCD_BUFFER_LENGTH / draw_width;
    int lines_remaining = draw_height;
    int lines_updated = 0;
    int window_top = -1;

    for (int y = 0; y < draw_height;)
    {
        int lines_to_copy = RG_MIN(lines_per_buffer, lines_remaining);

        if (lines_to_copy < 1)
            break;

        // The vertical filter requires a block to start and end with unscaled lines
        if (filter_y)
        {
            while (lines_to_copy > 1 && (LINE_IS_REPEATED(y + lines_to_copy - 1) ||
                                         LINE_IS_REPEATED(y + lines_to_copy)))
                --lines_to_copy;
        }

        uint16_t *line_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        uint16_t *line_buffer_ptr = line_buffer;

        uint32_t checksum = 0xFFFFFFFF;
        bool need_update = !partial_update;

        for (int i = 0; i < lines_to_copy; ++i)
        {
            if (i > 0 && LINE_IS_REPEATED(y))
            {
                memcpy(line_buffer_ptr, line_buffer_ptr - draw_width, draw_width * 2);
                line_buffer_ptr += draw_width;
            }
            else
            {
                #define RENDER_LINE(PTR_TYPE, PIXEL) { \
                    PTR_TYPE *buffer = (PTR_TYPE *)(data + map_viewport_to_source_y[y] * stride);\
                    for (int xx = 0; xx < draw_width; ++xx) { \
                        int x = map_viewport_to_source_x[xx]; \
                        *line_buffer_ptr++ = (PIXEL); \
                    } \
                }
                if (format & RG_PIXEL_PALETTE)
                    RENDER_LINE(uint8_t, palette[buffer[x]])
                else if (format == RG_PIXEL_565_LE)
                    RENDER_LINE(uint16_t, (buffer[x] << 8) | (buffer[x] >> 8))
                else
                    RENDER_LINE(uint16_t, buffer[x])

                if (partial_update)
                {
                    checksum = rg_hash((void*)(line_buffer_ptr - draw_width), draw_width * 2);
                }
            }

            if (screen_line_checksum[draw_top + y] != checksum)
            {
                screen_line_checksum[draw_top + y] = checksum;
                need_update = true;
            }

            ++y;
        }

        if (filter_x && need_update)
        {
            for (int i = 0; i < lines_to_copy; ++i)
            {
                uint16_t *buffer = line_buffer + i * draw_width;
                for (int x = 1; x < draw_width - 1; ++x)
                {
                    if (map_viewport_to_source_x[x] == map_viewport_to_source_x[x - 1])
                    {
                        buffer[x] = blend_pixels(buffer[x - 1], buffer[x + 1]);
                    }
                }
            }
        }

        if (filter_y && need_update)
        {
            int top = y - lines_to_copy;
            for (int i = 1; i < lines_to_copy - 1; ++i)
            {
                if (LINE_IS_REPEATED(top + i))
                {
                    uint16_t *lineA = line_buffer + (i - 1) * draw_width;
                    uint16_t *lineB = line_buffer + (i + 0) * draw_width;
                    uint16_t *lineC = line_buffer + (i + 1) * draw_width;
                    for (size_t x = 0; x < draw_width; ++x)
                    {
                        lineB[x] = blend_pixels(lineA[x], lineC[x]);
                    }
                }
            }
        }

        if (need_update)
        {
            int left = display.screen.margins.left + draw_left;
            int top = display.screen.margins.top + draw_top + y - lines_to_copy;
            if (top != window_top)
                lcd_set_window(left, top, draw_width, lines_remaining);
            lcd_send_buffer(line_buffer, draw_width * lines_to_copy);
            window_top = top + lines_to_copy;
            lines_updated += lines_to_copy;
        }
        else
        {
            // Return unused buffer
            lcd_send_buffer(line_buffer, 0);
        }

        lines_remaining -= lines_to_copy;
    }

    if (lines_updated > draw_height * 0.80f)
        counters.fullFrames++;
    else
        counters.partFrames++;
    counters.busyTime += rg_system_timer() - time_start;
}

static void update_viewport_scaling(void)
{
    int screen_width = display.screen.width;
    int screen_height = display.screen.height;
    int viewport_left = 0;
    int viewport_top = 0;
    int src_width = display.source.width;
    int src_height = display.source.height;
    int new_width = src_width;
    int new_height = src_height;
    bool force_fit = false;

#if defined(RG_TOUCH_GAMEPAD_FRAME) && RG_TOUCH_GAMEPAD_FRAME
    if (use_touch_gamepad_frame())
    {
        screen_width = RG_TOUCH_GAMEPAD_VIEW_WIDTH;
        screen_height = RG_TOUCH_GAMEPAD_VIEW_HEIGHT;
        viewport_left = RG_TOUCH_GAMEPAD_VIEW_LEFT;
        viewport_top = RG_TOUCH_GAMEPAD_VIEW_TOP;
        force_fit = true;
    }
#endif

    if (config.scaling == RG_DISPLAY_SCALING_FULL && !force_fit)
    {
        new_width = screen_width;
        new_height = screen_height;
    }
    else if (config.scaling == RG_DISPLAY_SCALING_FIT || force_fit)
    {
        new_width = FLOAT_TO_INT(screen_height * ((float)src_width / src_height));
        new_height = screen_height;
        if (new_width > screen_width) {
            new_width = screen_width;
            new_height = FLOAT_TO_INT(screen_width * ((float)src_height / src_width));
        }
    }
    else if (config.scaling == RG_DISPLAY_SCALING_ZOOM)
    {
        new_width = FLOAT_TO_INT(src_width * config.custom_zoom);
        new_height = FLOAT_TO_INT(src_height * config.custom_zoom);
    }

    // Everything works better when we use even dimensions!
    new_width &= ~1;
    new_height &= ~1;

    display.viewport.left = viewport_left + (screen_width - new_width) / 2;
    display.viewport.top = viewport_top + (screen_height - new_height) / 2;
    display.viewport.width = new_width;
    display.viewport.height = new_height;

    display.viewport.step_x = (float)src_width / display.viewport.width;
    display.viewport.step_y = (float)src_height / display.viewport.height;

    display.viewport.filter_x = (config.filter == RG_DISPLAY_FILTER_HORIZ || config.filter == RG_DISPLAY_FILTER_BOTH) &&
                                (config.scaling && (display.viewport.width % src_width) != 0);
    display.viewport.filter_y = (config.filter == RG_DISPLAY_FILTER_VERT || config.filter == RG_DISPLAY_FILTER_BOTH) &&
                                (config.scaling && (display.viewport.height % src_height) != 0);

    memset(screen_line_checksum, 0, sizeof(screen_line_checksum));

    for (int x = 0; x < display.viewport.width; ++x)
        map_viewport_to_source_x[x] = FLOAT_TO_INT(x * display.viewport.step_x);
    for (int y = 0; y < display.viewport.height; ++y)
        map_viewport_to_source_y[y] = FLOAT_TO_INT(y * display.viewport.step_y);

    RG_LOGI("%dx%d@%.3f => %dx%d@%.3f left:%d top:%d step_x:%.2f step_y:%.2f", src_width, src_height,
            (float)src_width / src_height, new_width, new_height, (float)new_width / new_height,
            display.viewport.left, display.viewport.top, display.viewport.step_x, display.viewport.step_y);
}

static bool load_border_file(const char *filename)
{
    RG_LOGI("Loading border file: %s", filename ?: "(none)");

    free(border), border = NULL;
    display.changed = true;

    if (filename && (border = rg_surface_load_image_file(filename, 0)))
    {
        if (border->width != rg_display_get_width() || border->height != rg_display_get_height())
        {
            rg_surface_t *resized = rg_surface_resize(border, rg_display_get_width(), rg_display_get_height());
            if (resized)
            {
                rg_surface_free(border);
                border = resized;
            }
        }
        return true;
    }
    return false;
}

IRAM_ATTR
static void display_task(void *arg)
{
    rg_task_msg_t msg;

    while (rg_task_peek(&msg))
    {
        // Received a shutdown request!
        if (msg.type == RG_TASK_MSG_STOP)
            break;

        if (display.changed)
        {
            update_viewport_scaling();
#if defined(RG_TOUCH_GAMEPAD_FRAME) && RG_TOUCH_GAMEPAD_FRAME
            if (use_touch_gamepad_frame())
            {
                lcd_sync();
            }
            else
#endif
            // Clear the screen if the viewport doesn't cover the entire screen because garbage could remain on the sides
            if (display.viewport.width < display.screen.width || display.viewport.height < display.screen.height)
            {
                if (border)
                    rg_display_write_rect(0, 0, border->width, border->height, 0, border->data, RG_DISPLAY_WRITE_NOSYNC);
                else
                    rg_display_clear_except(display.viewport.left, display.viewport.top, display.viewport.width, display.viewport.height, C_BLACK);
            }
            display.changed = false;
        }

        write_update(msg.dataPtr);
        draw_fps_overlay();

        rg_task_receive(&msg);

        lcd_sync();
    }
}

void rg_display_force_redraw(void)
{
    display.changed = true;
    // memset(screen_line_checksum, 0, sizeof(screen_line_checksum));
    rg_system_event(RG_EVENT_REDRAW, NULL);
    rg_display_sync(true);
}

const rg_display_t *rg_display_get_info(void)
{
    return &display;
}

rg_display_counters_t rg_display_get_counters(void)
{
    return counters;
}

int rg_display_get_width(void)
{
    // return display.screen.real_width - (display.screen.margins.left + display.screen.margins.right);
    return display.screen.width;
}

int rg_display_get_height(void)
{
    // return display.screen.real_height - (display.screen.margins.top + display.screen.margins.bottom);
    return display.screen.height;
}

void rg_display_set_scaling(display_scaling_t scaling)
{
    config.scaling = RG_MIN(RG_MAX(0, scaling), RG_DISPLAY_SCALING_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_SCALING, config.scaling);
    display.changed = true;
}

display_scaling_t rg_display_get_scaling(void)
{
    return config.scaling;
}

void rg_display_set_custom_zoom(double factor)
{
    config.custom_zoom = RG_MIN(RG_MAX(0.1, factor), 2.0);
    rg_settings_set_number(NS_APP, SETTING_CUSTOM_ZOOM, config.custom_zoom);
    display.changed = true;
}

double rg_display_get_custom_zoom(void)
{
    return config.custom_zoom;
}

void rg_display_set_fps_overlay(bool enabled)
{
    config.show_fps = enabled;
    rg_settings_set_boolean(NS_GLOBAL, SETTING_SHOW_FPS, enabled);
    display.changed = true;
}

bool rg_display_get_fps_overlay(void)
{
    return config.show_fps;
}

void rg_display_set_filter(display_filter_t filter)
{
    config.filter = RG_MIN(RG_MAX(0, filter), RG_DISPLAY_FILTER_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_FILTER, config.filter);
    display.changed = true;
}

display_filter_t rg_display_get_filter(void)
{
    return config.filter;
}

void rg_display_set_rotation(display_rotation_t rotation)
{
    config.rotation = RG_MIN(RG_MAX(0, rotation), RG_DISPLAY_ROTATION_COUNT - 1);
    rg_settings_set_number(NS_APP, SETTING_SCALING, config.rotation);
    display.changed = true;
}

display_rotation_t rg_display_get_rotation(void)
{
    return config.rotation;
}

void rg_display_set_backlight(display_backlight_t percent)
{
    config.backlight = RG_MIN(RG_MAX(percent, RG_DISPLAY_BACKLIGHT_MIN), RG_DISPLAY_BACKLIGHT_MAX);
    rg_settings_set_number(NS_GLOBAL, SETTING_BACKLIGHT, config.backlight);
    lcd_set_backlight(config.backlight);
}

display_backlight_t rg_display_get_backlight(void)
{
    return config.backlight;
}

void rg_display_set_border(const char *filename)
{
    free(config.border_file);
    config.border_file = NULL;

    if (load_border_file(filename))
    {
        rg_settings_set_string(NS_APP, SETTING_BORDER, filename);
        config.border_file = strdup(filename);
    }
    else
    {
        rg_settings_set_string(NS_APP, SETTING_BORDER, NULL);
        config.border_file = NULL;
    }
    display.changed = true;
}

char *rg_display_get_border(void)
{
    return rg_settings_get_string(NS_APP, SETTING_BORDER, NULL);
}

void rg_display_submit(const rg_surface_t *update, uint32_t flags)
{
    const int64_t time_start = rg_system_timer();

    // Those things should probably be asserted, but this is a new system let's be forgiving...
    if (!update || !update->data)
        return;

    if (display.source.width != update->width || display.source.height != update->height)
    {
        rg_display_sync(true);
        display.source.width = update->width;
        display.source.height = update->height;
        display.changed = true;
    }

    rg_task_send(display_task_queue, &(rg_task_msg_t){.dataPtr = update});

    counters.blockTime += rg_system_timer() - time_start;
    counters.totalFrames++;
}

bool rg_display_sync(bool block)
{
    while (block && rg_task_messages_waiting(display_task_queue))
        continue; // We should probably yield?
    return !rg_task_messages_waiting(display_task_queue);
}

void rg_display_write_rect(int left, int top, int width, int height, int stride, const uint16_t *buffer, uint32_t flags)
{
    RG_ASSERT_ARG(buffer);

    // calc stride before clipping width
    stride = RG_MAX(stride, width * 2);

    // Clipping
    width = RG_MIN(width, display.screen.width - left);
    height = RG_MIN(height, display.screen.height - top);

    // This can happen when left or top is out of bound
    if (width < 0 || height < 0)
        return;

    // This will work for now because we rarely draw from different threads (so all we need is ensure
    // that we're not interrupting a display update). But what we SHOULD be doing is acquire a lock
    // before every call to lcd_set_window and release it only after the last call to lcd_send_buffer.
    if (!(flags & RG_DISPLAY_WRITE_NOSYNC))
        rg_display_sync(true);

    // This isn't really necessary but it makes sense to invalidate
    // the lines we're about to overwrite...
    for (size_t y = 0; y < height; ++y)
        screen_line_checksum[top + y] = 0;

    lcd_set_window(left + display.screen.margins.left, top + display.screen.margins.top, width, height);

    for (size_t y = 0; y < height;)
    {
        uint16_t *lcd_buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
        size_t num_lines = RG_MIN(LCD_BUFFER_LENGTH / width, height - y);

        // Copy line by line because stride may not match width
        for (size_t line = 0; line < num_lines; ++line)
        {
            uint16_t *src = (void *)buffer + ((y + line) * stride);
            uint16_t *dst = lcd_buffer + (line * width);
            if (flags & RG_DISPLAY_WRITE_NOSWAP)
            {
                memcpy(dst, src, width * 2);
            }
            else
            {
                for (size_t i = 0; i < width; ++i)
                    dst[i] = (src[i] >> 8) | (src[i] << 8);
            }
        }

        lcd_send_buffer(lcd_buffer, width * num_lines);
        y += num_lines;
    }

    lcd_sync();
}

void rg_display_clear_rect(int left, int top, int width, int height, uint16_t color_le)
{
    const uint16_t color_be = (color_le << 8) | (color_le >> 8);
    int pixels_remaining = width * height;
    if (pixels_remaining > 0)
    {
        lcd_set_window(left + display.screen.margins.left, top + display.screen.margins.top, width, height);
        while (pixels_remaining > 0)
        {
            uint16_t *buffer = lcd_get_buffer(LCD_BUFFER_LENGTH);
            int pixels = RG_MIN(pixels_remaining, LCD_BUFFER_LENGTH);
            for (size_t j = 0; j < pixels; ++j)
                buffer[j] = color_be;
            lcd_send_buffer(buffer, pixels);
            pixels_remaining -= pixels;
        }
    }
}

void rg_display_clear_except(int left, int top, int width, int height, uint16_t color_le)
{
    // Clear everything except the specified area
    // FIXME: Do not ignore left/top...
    int left_offset = -display.screen.margins.left;
    int top_offset = -display.screen.margins.top;
    int horiz = (display.screen.real_width - width + 1) / 2;
    int vert = (display.screen.real_height - height + 1) / 2;
    rg_display_clear_rect(left_offset, top_offset, horiz, display.screen.real_height, color_le); // Left
    rg_display_clear_rect(left_offset + horiz + width, top_offset, horiz, display.screen.real_height, color_le); // Right
    rg_display_clear_rect(left_offset + horiz, top_offset, display.screen.real_width - horiz * 2, vert, color_le); // Top
    rg_display_clear_rect(left_offset + horiz, top_offset + vert + height, display.screen.real_width - horiz * 2, vert, color_le); // Bottom
}

void rg_display_clear(uint16_t color_le)
{
    // We ignore margins here, we want to fill the entire screen
    rg_display_clear_rect(-display.screen.margins.left, -display.screen.margins.top, display.screen.real_width,
                          display.screen.real_height, color_le);
}

void rg_display_deinit(void)
{
    rg_task_send(display_task_queue, &(rg_task_msg_t){.type = RG_TASK_MSG_STOP});
    // lcd_set_backlight(0);
    lcd_deinit();
    RG_LOGI("Display terminated.\n");
}

void rg_display_init(void)
{
    RG_LOGI("Initialization...\n");
    // TO DO: We probably should call the setters to ensure valid values...
    config = (rg_display_config_t){
        .backlight = rg_settings_get_number(NS_GLOBAL, SETTING_BACKLIGHT, 80),
        .scaling = rg_settings_get_number(NS_APP, SETTING_SCALING, RG_DISPLAY_SCALING_FIT),
        .filter = rg_settings_get_number(NS_APP, SETTING_FILTER, RG_DISPLAY_FILTER_BOTH),
        .rotation = rg_settings_get_number(NS_APP, SETTING_ROTATION, RG_DISPLAY_ROTATION_AUTO),
        .border_file = rg_settings_get_string(NS_APP, SETTING_BORDER, NULL),
        .custom_zoom = rg_settings_get_number(NS_APP, SETTING_CUSTOM_ZOOM, 1.0),
        .show_fps = rg_settings_get_boolean(NS_GLOBAL, SETTING_SHOW_FPS, false),
    };
    display = (rg_display_t){
        .screen.real_width = RG_SCREEN_WIDTH,
        .screen.real_height = RG_SCREEN_HEIGHT,
        .screen.width = RG_SCREEN_WIDTH,
        .screen.height = RG_SCREEN_HEIGHT,
        .screen.margins = RG_SCREEN_VISIBLE_AREA,
        .changed = true,
    };
    display.screen.width -= display.screen.margins.left + display.screen.margins.right;
    display.screen.height -= display.screen.margins.top + display.screen.margins.bottom;
    lcd_init();
    rg_display_clear(C_BLACK);
    rg_task_delay(80); // Wait for the screen be cleared before turning on the backlight (40ms doesn't seem to be enough...)
    lcd_set_backlight(config.backlight);
    display_task_queue = rg_task_create("rg_display", &display_task, NULL, 4 * 1024, RG_TASK_PRIORITY_6, 1);
    if (config.border_file)
        load_border_file(config.border_file);
    RG_LOGI("Display ready.\n");
}
