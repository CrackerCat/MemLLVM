#pragma once
#include <stdint.h>
#include <stddef.h>

class Chip8Core {
public:
    uint8_t  memory[4096];
    uint8_t  V[16];
    uint16_t I;
    uint16_t pc;
    uint8_t  gfx[64 * 32];
    uint8_t  delay_timer;
    uint8_t  sound_timer;
    uint16_t stack[16];
    uint16_t sp;
    uint8_t  key[16];

    bool     draw_flag;
    bool     paused;
    uint32_t rng_state;

    Chip8Core();
    void reset();
    bool load_rom(const uint8_t *data, size_t size);
    void emulate_cycle();
    void tick_timers();
    void set_key(int key_idx, bool pressed);

private:
    uint8_t get_random_byte();
};
