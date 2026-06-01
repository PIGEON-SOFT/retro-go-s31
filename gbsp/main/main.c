#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../components/gbsp-libretro/common.h"
#include "../components/gbsp-libretro/memmap.h"
#include "../components/gbsp-libretro/gba_memory.h"
#include "../components/gbsp-libretro/gba_cc_lut.h"

#define AUDIO_SAMPLE_RATE (GBA_SOUND_FREQUENCY)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

#include "bios.h"

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
boot_mode selected_boot_mode = boot_game;

u32 skip_next_frame = 0;
int sprite_limit = 1;

gbsp_memory_t *gbsp_memory;

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;
static char *backup_file;
static uint32_t backup_crc;
static bool backup_loaded;

static uint32_t backup_get_crc(void)
{
    return rg_crc32(0, gamepak_backup, sizeof(gamepak_backup));
}

static void backup_load(void)
{
    void *data = NULL;
    size_t data_len = 0;

    backup_file = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    rg_storage_mkdir(rg_dirname(backup_file));

    if (rg_storage_read_file(backup_file, &data, &data_len, 0))
    {
        size_t copy_len = data_len < sizeof(gamepak_backup) ? data_len : sizeof(gamepak_backup);
        memcpy(gamepak_backup, data, copy_len);
        free(data);
        backup_loaded = true;
        RG_LOGI("Loaded GBA backup: %s (%u bytes)", backup_file, (unsigned)copy_len);
    }

    backup_crc = backup_get_crc();
}

static bool backup_save(bool force)
{
    uint32_t crc = backup_get_crc();

    if (!force && crc == backup_crc)
        return true;

    if (backup_type == BACKUP_UNKN && !backup_loaded)
        return true;

    if (!backup_file)
        return false;

    if (!rg_storage_mkdir(rg_dirname(backup_file)))
        RG_LOGW("Could not create backup directory: %s", rg_dirname(backup_file));

    if (!rg_storage_write_file(backup_file, gamepak_backup, sizeof(gamepak_backup), RG_FILE_ATOMIC_WRITE))
    {
        RG_LOGE("Failed to save GBA backup: %s", backup_file);
        return false;
    }

    backup_crc = crc;
    backup_loaded = true;
    RG_LOGI("Saved GBA backup: %s", backup_file);
    return true;
}

void netpacket_poll_receive()
{
}

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    void *data = malloc(GBA_STATE_MEM_SIZE);
    if (!data)
        return false;

    gba_save_state(data);
    bool ok = rg_storage_write_file(filename, data, GBA_STATE_MEM_SIZE, RG_FILE_ATOMIC_WRITE);
    free(data);
    backup_save(true);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    void *data = NULL;
    size_t data_len = 0;

    if (!rg_storage_read_file(filename, &data, &data_len, 0))
        return false;

    bool ok = data_len == GBA_STATE_MEM_SIZE && gba_load_state(data);
    free(data);

    if (ok)
        backup_crc = backup_get_crc();

    return ok;
}

static bool reset_handler(bool hard)
{
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        rg_display_submit(currentUpdate, 0);
    }
    else if (event == RG_EVENT_SHUTDOWN)
    {
        backup_save(true);
    }
}

int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    // RG_LOGI("%u, %u, %u, %u", port, device, index, id);
    uint32_t joystick = rg_input_read_gamepad();
    int16_t val = 0;
    if (joystick & RG_KEY_DOWN) val |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (joystick & RG_KEY_UP) val |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (joystick & RG_KEY_LEFT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (joystick & RG_KEY_RIGHT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (joystick & RG_KEY_START) val |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (joystick & RG_KEY_SELECT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (joystick & RG_KEY_B) val |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (joystick & RG_KEY_A) val |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (joystick & RG_KEY_L) val |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (joystick & RG_KEY_R) val |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    return val;
}

void set_fastforward_override(bool fastforward)
{
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);
    // app = rg_system_init(AUDIO_SAMPLE_RATE * 0.7, &handlers, NULL);
    // rg_system_set_overclock(2);

    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    updates[0]->height = GBA_SCREEN_HEIGHT;
    // updates[1] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    // updates[1]->height = GBA_SCREEN_HEIGHT;
    currentUpdate = updates[0];

    gba_screen_pixels = currentUpdate->data;

    gbsp_memory = rg_alloc(sizeof(*gbsp_memory), MEM_ANY);
    RG_LOGI("gbsp_memory=%p", gbsp_memory);

    libretro_supports_bitmasks = true;
    retro_set_input_state(input_cb);
    init_gamepak_buffer();
    init_sound();

    if (load_bios(RG_BASE_PATH_BIOS "/gba_bios.bin") != 0)
        memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));

    const char *rom_path = app->romPath;
    if (!rom_path || !rom_path[0])
        RG_PANIC("No GBA ROM selected.");

    RG_LOGI("Loading GBA ROM: %s", rom_path);

    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    if (load_gamepak(NULL, rom_path, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
    {
        RG_PANIC("Could not load the game file.");
    }

    backup_load();

    RG_LOGI("reset_gba");
    reset_gba();

    RG_LOGI("emulation loop");

    while (true)
    {
        // RG_TIMER_INIT();
        static uint32_t backup_autosave_counter;

        rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH];
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t start_time = rg_system_timer();

        update_input();
        rumble_frame_reset();

        clear_gamepak_stickybits();
        execute_arm(execute_cycles);
        // RG_TIMER_LAP("execute_arm");

        if (!skip_next_frame)
            rg_display_submit(currentUpdate, 0);

        size_t frames_count = sound_read_samples((s16 *)mixbuffer, AUDIO_BUFFER_LENGTH);
        // RG_TIMER_LAP("sound_read_samples");

        rg_system_tick(rg_system_timer() - start_time);

        rg_audio_submit(mixbuffer, frames_count);
        // RG_TIMER_LAP("rg_audio_submit");

        if (skip_next_frame == 0)
            skip_next_frame = app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;

        if (++backup_autosave_counter >= 120)
        {
            backup_autosave_counter = 0;
            backup_save(false);
        }
    }

    RG_PANIC("GBsP Ended");
}
