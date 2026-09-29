
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ff.h"
#include "pico/stdlib.h"

#include "emu6502.h"
#include "jack_wave1_audio.h"
#include "jack_wave1_perf.h"
#include "jack_wave1_runtime.h"

namespace {
constexpr uint16_t kScreen = 0x1e00;
constexpr uint16_t kJoystick = 0x9111;
// The demo PRG jumps here once per frame.
constexpr uint16_t kHostTrap = 0x9000;
constexpr uint32_t kAudioRate = 44100u;
constexpr uint32_t kAudioFramesPerVideoFrame = kAudioRate / 60u;
byte g_memory[65536];
JackWave1Perf g_perf;
uint32_t g_resume_us;
char g_path[JACK_WAVE1_PATH_MAX];
byte g_previous_cells[22];
byte g_previous_score = 0xff;
bool g_dynamic_frame_valid = false;
int16_t g_audio[kAudioFramesPerVideoFrame * 2u];
uint32_t g_audio_phase = 0u;
uint32_t g_audio_frequency = 0u;
uint8_t g_audio_frames_left = 0u;

void start_tone(uint32_t frequency, uint8_t video_frames)
{
    g_audio_frequency = frequency;
    g_audio_frames_left = video_frames;
}

void submit_audio_frame(byte previous_score, byte score)
{
    if (score > previous_score) {

        start_tone((score & 1u) != 0u ? 880u : 1040u, 5u);
    } else if (score < previous_score) {
        start_tone(330u, 4u);
    }

    const uint32_t step = g_audio_frequency * 2u;
    for (size_t frame = 0; frame < kAudioFramesPerVideoFrame; ++frame) {
        int16_t sample = 0;
        if (g_audio_frames_left != 0u && g_audio_frequency != 0u) {
            sample = g_audio_phase < kAudioRate ? 5500 : -5500;
            g_audio_phase += step;
            if (g_audio_phase >= kAudioRate * 2u)
                g_audio_phase -= kAudioRate * 2u;
        }
        g_audio[frame * 2u] = sample;
        g_audio[frame * 2u + 1u] = sample;
    }
    if (g_audio_frames_left != 0u) --g_audio_frames_left;
    jack_wave1_audio_submit(g_audio, kAudioFramesPerVideoFrame);
}

class VicMemory final : public Emu6502::Memory {
public:
    byte read(ushort address) override { return g_memory[address]; }
    void write(ushort address, byte value) override { g_memory[address] = value; }
};

void draw_static_frame()
{
    const uint16_t black = 0;
    const uint16_t blue = jack_wave1_rgb555(3, 8, 20);
    const uint16_t cyan = jack_wave1_rgb555(4, 28, 28);
    jack_wave1_clear(blue);
    jack_wave1_draw_text(72, 20, "VIC-20 CATCH", cyan, blue);
    jack_wave1_draw_text(40, 44, "D-PAD: MOVE   A: RESET SCORE", cyan, blue);
    constexpr unsigned cell_w = 12;
    constexpr unsigned left = (JACK_WAVE1_SCREEN_WIDTH - 22 * cell_w) / 2;
    constexpr unsigned top = 100;
    jack_wave1_fill_rect(left - 3, top - 3, 22 * cell_w + 6, 30, black);
    jack_wave1_draw_text(56, 154, "CATCH THE YELLOW BLOCK", cyan, blue);
    jack_wave1_draw_text(48, 180, "6502 PRG + VIC SCREEN RAM", cyan, blue);
}

void draw_dynamic_frame()
{
    const uint16_t blue = jack_wave1_rgb555(3, 8, 20);
    const uint16_t cyan = jack_wave1_rgb555(4, 28, 28);
    const uint16_t yellow = jack_wave1_rgb555(31, 28, 3);
    if (!g_dynamic_frame_valid || g_previous_score != g_memory[2]) {
        jack_wave1_fill_rect(120, 64, 112, 8, blue);
        char score[40];
        snprintf(score, sizeof(score), "SCORE %u", static_cast<unsigned>(g_memory[2]));
        jack_wave1_draw_text(120, 64, score, yellow, blue);
        g_previous_score = g_memory[2];
    }

    constexpr unsigned cell_w = 12;
    constexpr unsigned left = (JACK_WAVE1_SCREEN_WIDTH - 22 * cell_w) / 2;
    constexpr unsigned top = 100;
    for (unsigned x = 0; x < 22; ++x) {
        const byte cell = g_memory[kScreen + x];
        if (g_dynamic_frame_valid && g_previous_cells[x] == cell) continue;
        uint16_t color = blue;
        if (cell == 1) color = cyan;
        else if (cell == 2) color = yellow;
        jack_wave1_fill_rect(left + x * cell_w, top, cell_w - 2, 24, color);
        g_previous_cells[x] = cell;
    }
    g_dynamic_frame_valid = true;
}

class VicCpu final : public Emu6502 {
public:
    VicCpu() : Emu6502(new VicMemory()) {}

protected:
    bool ExecutePatch() override
    {
        if (PC != kHostTrap) return false;
        const uint32_t video_frame = jack_wave1_frame_counter();
        const uint32_t trap_us = time_us_32();
        const uint32_t core_us = trap_us - g_resume_us;
        const uint16_t buttons = jack_wave1_poll_buttons();
        const byte previous_score = g_previous_score;
        byte joystick = 0;
        if (buttons & JACK_BUTTON_LEFT) joystick |= 0x01;
        if (buttons & JACK_BUTTON_RIGHT) joystick |= 0x02;
        if (buttons & (JACK_BUTTON_A | JACK_BUTTON_B | JACK_BUTTON_X)) joystick |= 0x04;
        g_memory[kJoystick] = joystick;

        const uint32_t draw_start = time_us_32();
        draw_dynamic_frame();
        submit_audio_frame(previous_score, g_memory[2]);
        const uint32_t draw_end = time_us_32();
        const bool waited = jack_wave1_wait_frame_boundary(video_frame);
        const uint32_t wait_end = time_us_32();
        jack_wave1_audio_report();
        jack_wave1_usb_service();
        g_perf.sample("VIC20", core_us, draw_end - draw_start, wait_end - draw_end,
                      buttons, waited ? "running" : "late", jack_wave1_perf_stdout);
        g_resume_us = wait_end;
        return false;
    }
};

bool load_prg(const char *path, uint16_t *load_address, size_t *payload_size)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return false;
    uint8_t header[2];
    UINT read = 0;
    bool ok = f_read(&file, header, sizeof(header), &read) == FR_OK && read == sizeof(header);
    const uint16_t address = static_cast<uint16_t>(header[0] | (header[1] << 8));
    const FSIZE_t total = f_size(&file);
    const size_t payload = total >= 2 ? static_cast<size_t>(total - 2) : 0;
    if (!ok || payload == 0 || static_cast<size_t>(address) + payload > sizeof(g_memory)) {
        f_close(&file);
        return false;
    }
    ok = f_read(&file, g_memory + address, static_cast<UINT>(payload), &read) == FR_OK &&
         read == payload;
    f_close(&file);
    if (!ok) return false;
    *load_address = address;
    *payload_size = payload;
    return true;
}
}

int main()
{
    if (!jack_wave1_init("VIC-20")) for (;;) tight_loop_contents();
    if (!jack_wave1_audio_begin(kAudioRate)) {
        jack_wave1_show_error("VIC-20", "HDMI audio init failed");
        for (;;) tight_loop_contents();
    }
    printf("VIC20_BOOT version=0.2.0-fruit-jam-candidate uart=0 tx=44 rx=45 baud=115200 audio=44100x2\n");
    if (!jack_wave1_mount_sd()) {
        jack_wave1_show_error("VIC-20", "SD mount failed");
        for (;;) tight_loop_contents();
    }
    jack_wave1_set_picker_auto_select_single(false);
    if (!jack_wave1_select_file("SELECT VIC-20 TEST PRG", "/roms/VIC20", ".prg",
                                g_path, sizeof(g_path))) {
        jack_wave1_show_error("VIC-20", "No .prg test game");
        for (;;) tight_loop_contents();
    }
    memset(g_memory, 0, sizeof(g_memory));
    uint16_t load_address = 0;
    size_t payload_size = 0;
    if (!load_prg(g_path, &load_address, &payload_size)) {
        jack_wave1_show_error("VIC-20", "Invalid PRG");
        for (;;) tight_loop_contents();
    }
    g_memory[0xfffc] = static_cast<byte>(load_address);
    g_memory[0xfffd] = static_cast<byte>(load_address >> 8);
    printf("VIC20_PRG path=%s load=%04x bytes=%lu trap=%04x\n", g_path,
           load_address, static_cast<unsigned long>(payload_size), kHostTrap);
    printf("VIC20_START mode=6502-test-game screen=1e00 joystick=9111 controls=left/right,a-reset\n");
    jack_wave1_wait_buttons_released();
    jack_wave1_wait_vsync();
    draw_static_frame();

    // Avoid treating the initial score as a reset.
    g_previous_score = g_memory[2];
    g_dynamic_frame_valid = false;
    g_perf.begin();
    g_resume_us = time_us_32();
    VicCpu cpu;
    cpu.ResetRun();
    jack_wave1_show_error("VIC-20", "6502 program stopped");
    for (;;) tight_loop_contents();
}
