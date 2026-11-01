#include "chip8_core.h"

static const uint8_t g_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

Chip8Core::Chip8Core() {
    rng_state = 0x12345678;
    paused = false;
    reset();
}

void Chip8Core::reset() {
    pc = 0x200;
    I = 0;
    sp = 0;
    delay_timer = 0;
    sound_timer = 0;
    draw_flag = true;

    for (int i = 0; i < 4096; ++i) memory[i] = 0;
    for (int i = 0; i < 16; ++i) {
        V[i] = 0;
        stack[i] = 0;
        key[i] = 0;
    }
    for (int i = 0; i < 64 * 32; ++i) gfx[i] = 0;

    // Load fontset at both 0x000 and 0x050 for maximum ROM compatibility
    for (int i = 0; i < 80; ++i) {
        memory[i] = g_fontset[i];
        memory[0x50 + i] = g_fontset[i];
    }
}

bool Chip8Core::load_rom(const uint8_t *data, size_t size) {
    if (!data || size == 0 || size > (4096 - 512)) return false;
    reset();
    for (size_t i = 0; i < size; ++i) {
        memory[0x200 + i] = data[i];
    }
    return true;
}

uint8_t Chip8Core::get_random_byte() {
    // Xorshift32 PRNG
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (uint8_t)(rng_state & 0xFF);
}

void Chip8Core::set_key(int key_idx, bool pressed) {
    if (key_idx >= 0 && key_idx < 16) {
        key[key_idx] = pressed ? 1 : 0;
    }
}

void Chip8Core::tick_timers() {
    if (delay_timer > 0) --delay_timer;
    if (sound_timer > 0) --sound_timer;
}

void Chip8Core::emulate_cycle() {
    if (paused || pc > 4094) return;

    uint16_t opcode = (memory[pc] << 8) | memory[pc + 1];
    uint16_t nnn = opcode & 0x0FFF;
    uint8_t  nn  = (uint8_t)(opcode & 0x00FF);
    uint8_t  x   = (opcode & 0x0F00) >> 8;
    uint8_t  y   = (opcode & 0x00F0) >> 4;
    uint8_t  n   = opcode & 0x000F;

    switch (opcode & 0xF000) {
    case 0x0000:
        if (opcode == 0x00E0) {
            // CLS: Clear the display
            for (int i = 0; i < 64 * 32; ++i) gfx[i] = 0;
            draw_flag = true;
            pc += 2;
        } else if (opcode == 0x00EE) {
            // RET: Return from subroutine
            if (sp > 0) {
                --sp;
                pc = stack[sp];
            }
            pc += 2;
        } else {
            // 0NNN: SYS call, ignore and advance
            pc += 2;
        }
        break;

    case 0x1000:
        // JP addr
        pc = nnn;
        break;

    case 0x2000:
        // CALL addr
        if (sp < 16) {
            stack[sp] = pc;
            ++sp;
            pc = nnn;
        } else {
            pc += 2; // Stack overflow safeguard
        }
        break;

    case 0x3000:
        // SE Vx, byte
        pc += (V[x] == nn) ? 4 : 2;
        break;

    case 0x4000:
        // SNE Vx, byte
        pc += (V[x] != nn) ? 4 : 2;
        break;

    case 0x5000:
        // SE Vx, Vy
        pc += (V[x] == V[y]) ? 4 : 2;
        break;

    case 0x6000:
        // LD Vx, byte
        V[x] = nn;
        pc += 2;
        break;

    case 0x7000:
        // ADD Vx, byte
        V[x] += nn;
        pc += 2;
        break;

    case 0x8000:
        switch (n) {
        case 0x0:
            // LD Vx, Vy
            V[x] = V[y];
            pc += 2;
            break;
        case 0x1:
            // OR Vx, Vy
            V[x] |= V[y];
            pc += 2;
            break;
        case 0x2:
            // AND Vx, Vy
            V[x] &= V[y];
            pc += 2;
            break;
        case 0x3:
            // XOR Vx, Vy
            V[x] ^= V[y];
            pc += 2;
            break;
        case 0x4: {
            // ADD Vx, Vy (VF = carry)
            uint16_t sum = (uint16_t)V[x] + (uint16_t)V[y];
            V[x] = (uint8_t)sum;
            V[0xF] = (sum > 0xFF) ? 1 : 0;
            pc += 2;
            break;
        }
        case 0x5: {
            // SUB Vx, Vy (VF = NOT borrow)
            uint8_t flag = (V[x] >= V[y]) ? 1 : 0;
            V[x] -= V[y];
            V[0xF] = flag;
            pc += 2;
            break;
        }
        case 0x6: {
            // SHR Vx
            uint8_t flag = V[x] & 1;
            V[x] >>= 1;
            V[0xF] = flag;
            pc += 2;
            break;
        }
        case 0x7: {
            // SUBN Vx, Vy (VF = NOT borrow, Vy - Vx)
            uint8_t flag = (V[y] >= V[x]) ? 1 : 0;
            V[x] = V[y] - V[x];
            V[0xF] = flag;
            pc += 2;
            break;
        }
        case 0xE: {
            // SHL Vx
            uint8_t flag = (V[x] >> 7) & 1;
            V[x] <<= 1;
            V[0xF] = flag;
            pc += 2;
            break;
        }
        default:
            pc += 2;
            break;
        }
        break;

    case 0x9000:
        // SNE Vx, Vy
        pc += (V[x] != V[y]) ? 4 : 2;
        break;

    case 0xA000:
        // LD I, addr
        I = nnn;
        pc += 2;
        break;

    case 0xB000:
        // JP V0, addr
        pc = nnn + V[0];
        break;

    case 0xC000:
        // RND Vx, byte
        V[x] = get_random_byte() & nn;
        pc += 2;
        break;

    case 0xD000: {
        // DRW Vx, Vy, nibble
        uint8_t vx = V[x];
        uint8_t vy = V[y];
        V[0xF] = 0;
        for (int row = 0; row < n; ++row) {
            if (I + row >= 4096) break;
            uint8_t sprite = memory[I + row];
            int py = (vy + row) % 32;
            for (int col = 0; col < 8; ++col) {
                if ((sprite & (0x80 >> col)) != 0) {
                    int px = (vx + col) % 64;
                    int idx = px + py * 64;
                    if (gfx[idx] == 1) {
                        V[0xF] = 1;
                    }
                    gfx[idx] ^= 1;
                }
            }
        }
        draw_flag = true;
        pc += 2;
        break;
    }

    case 0xE000:
        if (nn == 0x9E) {
            // SKP Vx
            pc += key[V[x] & 0xF] ? 4 : 2;
        } else if (nn == 0xA1) {
            // SKNP Vx
            pc += (!key[V[x] & 0xF]) ? 4 : 2;
        } else {
            pc += 2;
        }
        break;

    case 0xF000:
        switch (nn) {
        case 0x07:
            // LD Vx, DT
            V[x] = delay_timer;
            pc += 2;
            break;
        case 0x0A: {
            // LD Vx, K (Wait for key)
            bool pressed = false;
            for (int k = 0; k < 16; ++k) {
                if (key[k]) {
                    V[x] = (uint8_t)k;
                    pressed = true;
                    break;
                }
            }
            if (!pressed) return; // Do not advance PC
            pc += 2;
            break;
        }
        case 0x15:
            // LD DT, Vx
            delay_timer = V[x];
            pc += 2;
            break;
        case 0x18:
            // LD ST, Vx
            sound_timer = V[x];
            pc += 2;
            break;
        case 0x1E:
            // ADD I, Vx
            I += V[x];
            pc += 2;
            break;
        case 0x29:
            // LD F, Vx
            I = (V[x] & 0xF) * 5;
            pc += 2;
            break;
        case 0x33:
            // LD B, Vx (BCD)
            if (I + 2 < 4096) {
                memory[I]     = V[x] / 100;
                memory[I + 1] = (V[x] / 10) % 10;
                memory[I + 2] = V[x] % 10;
            }
            pc += 2;
            break;
        case 0x55:
            // LD [I], Vx
            for (int i = 0; i <= x && (I + i < 4096); ++i) {
                memory[I + i] = V[i];
            }
            I += x + 1;
            pc += 2;
            break;
        case 0x65:
            // LD Vx, [I]
            for (int i = 0; i <= x && (I + i < 4096); ++i) {
                V[i] = memory[I + i];
            }
            I += x + 1;
            pc += 2;
            break;
        default:
            pc += 2;
            break;
        }
        break;

    default:
        pc += 2;
        break;
    }
}
