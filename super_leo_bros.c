/*
 * Super Leo Bros.
 * Original copyright-free classic 2D platformer built for Raspberry Pi (RetroPie)
 * Features:
 *  - Classic momentum running, skidding, and variable jump height physics
 *  - 3-tier Power-up progression: Small Leo -> Super Leo -> Fire Leo + Starman
 *  - Mid-level Checkpoints, 5 Lives, 100 Coins = 1-Up, Unlimited Level Continues
 *  - 4-Stage World Arc:
 *      1-1 Sunny Overworld (with secret enterable underground coin pipe!)
 *      1-2 Underground Cavern
 *      1-3 Sky Athletic Bridges
 *      1-4 Lava Castle + Bridge Boss Fight
 *  - Built-in procedural pixel art & retro chiptune sound effects (zero external asset dependencies)
 */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdint.h>
typedef uint32_t Uint32;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint8_t  Uint8;
enum {
    SDLK_LEFT   = 1,
    SDLK_RIGHT  = 2,
    SDLK_DOWN   = 3,
    SDLK_UP     = 4,
    SDLK_SPACE  = 5,
    SDLK_z      = 6,
    SDLK_x      = 7,
    SDLK_a      = 8,
    SDLK_d      = 9,
    SDLK_s      = 10,
    SDLK_w      = 11,
    SDLK_LSHIFT = 12,
    SDLK_RSHIFT = 13,
    SDLK_LCTRL  = 14,
    SDLK_RETURN = 15,
    SDLK_ESCAPE = 16,
    SDLK_LAST   = 32
};
#else
#include <SDL/SDL.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define TILE_SIZE 16

#define MAP_W 220
#define MAP_H 15

#define MAX_ENTITIES 128
#define MAX_PARTICLES 128
#define MAX_FIREBALLS 8

/* Power-up states */
enum {
    STATE_SMALL = 0,
    STATE_SUPER = 1,
    STATE_FIRE  = 2
};

/* Entity types */
enum {
    ENT_NONE = 0,
    ENT_WALKER,       /* 1-stomp mushroom walker */
    ENT_TURTLE,       /* 2-state shell enemy */
    ENT_SHELL,        /* retracted/sliding shell */
    ENT_PLANT,        /* pop-up pipe chomper */
    ENT_BOSS,         /* 1-4 Castle Boss */
    ENT_BOSS_FIRE,    /* Boss fire breath */
    ENT_MUSHROOM,     /* Super growth mushroom */
    ENT_FLOWER,       /* Fire flower */
    ENT_STAR,         /* Invincibility star */
    ENT_ONEUP,        /* 1-Up green mushroom */
    ENT_PLATFORM,     /* Vertical moving platform */
    ENT_HPLATFORM,    /* Horizontal moving platform */
    ENT_FLY_TURTLE    /* Winged bouncing/flying turtle */
};

/* Sound effect IDs */
enum {
    SFX_NONE = 0,
    SFX_JUMP,
    SFX_COIN,
    SFX_POWERUP_APPEAR,
    SFX_POWERUP,
    SFX_SHRINK,
    SFX_STOMP,
    SFX_KICK,
    SFX_BRICK,
    SFX_BUMP,
    SFX_FIREBALL,
    SFX_ONEUP,
    SFX_FLAGPOLE,
    SFX_STAGE_CLEAR,
    SFX_DIE,
    SFX_CHECKPOINT
};

typedef struct {
    int active;
    int type;
    float x, y;
    float vx, vy;
    int w, h;
    int dir;
    int state;
    int timer;
    int on_ground;
    float start_x, start_y;
    int hp;
} Entity;

typedef struct {
    int active;
    float x, y;
    float vx, vy;
    int timer;
    int type; /* 0 = brick debris, 1 = coin popup, 2 = score text, 3 = fireball spark */
    int value;
    Uint32 color;
} Particle;

typedef struct {
    int active;
    float x, y;
    float vx, vy;
    int timer;
} Fireball;

typedef struct {
    float x, y;
    float vx, vy;
    int w, h;
    int dir;
    int power;          /* STATE_SMALL, STATE_SUPER, STATE_FIRE */
    int on_ground;
    int jump_hold;
    int invuln_timer;   /* post-damage blinking */
    int star_timer;     /* rainbow star invincibility */
    int crouch;
    int skidding;
    float anim_frame;
    int fire_cooldown;
    int dead;
    int dead_timer;
    int flagpole_state; /* 0 = playing, 1 = sliding pole, 2 = walking to castle */
    int flagpole_timer;
    int pipe_timer;
} Player;

/* Global Game State */
#ifdef __EMSCRIPTEN__
static Uint32 wasm_fb[SCREEN_W * SCREEN_H];
static Uint8 wasm_keys[SDLK_LAST];
#define SDL_Flip(s) ((void)0)
#else
static SDL_Surface *screen = NULL;
#endif
static char map[MAP_H][MAP_W + 1];
static int bump_offset[MAP_H][MAP_W];
static char bump_orig[MAP_H][MAP_W];

static Player player;
static Entity entities[MAX_ENTITIES];
static Particle particles[MAX_PARTICLES];
static Fireball fireballs[MAX_FIREBALLS];

static float cam_x = 0.0f;
static int current_level = 0; /* 0..3 = 1-1..1-4 */
static int in_bonus_room = 0;
static float return_x = 0.0f, return_y = 0.0f;

static int checkpoint_reached = 0;
static float checkpoint_x = 48.0f, checkpoint_y = 176.0f;

static int lives = 5;
static int coins = 0;
static int score = 0;
static int level_timer = 400;
static int frame_counter = 0;
static int firebar_angle = 0;
static int boss_bridge_collapse = 0;
static int game_won = 0;
static int title_screen = 1;
static int selected_start_level = 0;
static int max_unlocked_level = 11; /* All 12 stages (Worlds 1-1 to 3-4) accessible from title */

#ifndef __EMSCRIPTEN__
/* Audio synthesis state */
static int current_sfx = SFX_NONE;
static int sfx_pos = 0;
static int sfx_len = 0;
#endif

static void play_sfx(int id) {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (window.onLeoSfx) window.onLeoSfx($0);
    }, id);
#else
    SDL_LockAudio();
    current_sfx = id;
    sfx_pos = 0;
    switch (id) {
        case SFX_JUMP:           sfx_len = 3200; break;
        case SFX_COIN:           sfx_len = 4500; break;
        case SFX_POWERUP_APPEAR: sfx_len = 4000; break;
        case SFX_POWERUP:        sfx_len = 7000; break;
        case SFX_SHRINK:         sfx_len = 5000; break;
        case SFX_STOMP:          sfx_len = 2200; break;
        case SFX_KICK:           sfx_len = 2500; break;
        case SFX_BRICK:          sfx_len = 3000; break;
        case SFX_BUMP:           sfx_len = 1800; break;
        case SFX_FIREBALL:       sfx_len = 2000; break;
        case SFX_ONEUP:          sfx_len = 7500; break;
        case SFX_FLAGPOLE:       sfx_len = 9000; break;
        case SFX_STAGE_CLEAR:    sfx_len = 14000; break;
        case SFX_DIE:            sfx_len = 10000; break;
        case SFX_CHECKPOINT:     sfx_len = 5000; break;
        default:                 sfx_len = 0; break;
    }
    SDL_UnlockAudio();
#endif
}

#ifndef __EMSCRIPTEN__
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    Sint16 *buf = (Sint16 *)stream;
    int samples = len / 2;
    (void)userdata;

    for (int i = 0; i < samples; i++) {
        Sint16 out = 0;
        if (current_sfx != SFX_NONE && sfx_pos < sfx_len) {
            float prog = (float)sfx_pos / (float)sfx_len;
            float freq = 440.0f;
            int noise = 0;

            switch (current_sfx) {
                case SFX_JUMP:
                    freq = 220.0f + 420.0f * prog;
                    break;
                case SFX_COIN:
                    freq = (prog < 0.25f) ? 987.77f : 1318.51f;
                    break;
                case SFX_POWERUP_APPEAR:
                    freq = 330.0f + ((sfx_pos / 400) % 4) * 110.0f;
                    break;
                case SFX_POWERUP: {
                    static const float notes[8] = {330, 392, 659, 523, 587, 784, 659, 880};
                    int idx = (int)(prog * 8.0f);
                    if (idx > 7) idx = 7;
                    freq = notes[idx];
                    break;
                }
                case SFX_ONEUP: {
                    static const float notes[6] = {330, 392, 659, 523, 587, 784};
                    int idx = (int)(prog * 6.0f);
                    if (idx > 5) idx = 5;
                    freq = notes[idx] * 1.25f;
                    break;
                }
                case SFX_SHRINK:
                    freq = 600.0f - 400.0f * prog;
                    break;
                case SFX_STOMP:
                    freq = 400.0f - 220.0f * prog;
                    break;
                case SFX_KICK:
                    freq = 180.0f + 350.0f * (1.0f - prog);
                    break;
                case SFX_BRICK:
                    noise = 1;
                    break;
                case SFX_BUMP:
                    freq = 140.0f - 60.0f * prog;
                    break;
                case SFX_FIREBALL:
                    freq = 700.0f - 500.0f * prog;
                    break;
                case SFX_CHECKPOINT:
                    freq = (prog < 0.5f) ? 523.25f : 783.99f;
                    break;
                case SFX_FLAGPOLE:
                    freq = 220.0f + 660.0f * prog;
                    break;
                case SFX_STAGE_CLEAR: {
                    static const float notes[7] = {261.6f, 329.6f, 392.0f, 523.2f, 659.2f, 784.0f, 1046.5f};
                    int idx = (int)(prog * 7.0f);
                    if (idx > 6) idx = 6;
                    freq = notes[idx];
                    break;
                }
                case SFX_DIE: {
                    static const float notes[6] = {493.8f, 698.4f, 698.4f, 659.2f, 587.3f, 523.2f};
                    int idx = (int)(prog * 6.0f);
                    if (idx > 5) idx = 5;
                    freq = notes[idx];
                    break;
                }
            }

            if (noise) {
                out = (Sint16)(((rand() % 6000) - 3000) * (1.0f - prog));
            } else {
                int period = (int)(22050.0f / (freq > 40.0f ? freq : 40.0f));
                if (period < 2) period = 2;
                int sq = ((sfx_pos % period) < (period / 2)) ? 1 : -1;
                out = (Sint16)(sq * 2800 * (1.0f - 0.3f * prog));
            }
            sfx_pos++;
            if (sfx_pos >= sfx_len) current_sfx = SFX_NONE;
        }
        buf[i] = out;
    }
}
#endif

/* Drawing Primitives */
static void draw_rect(int x, int y, int w, int h, Uint32 color) {
    if (x >= SCREEN_W || y >= SCREEN_H || x + w <= 0 || y + h <= 0) return;
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = (x + w > SCREEN_W) ? SCREEN_W : (x + w);
    int y2 = (y + h > SCREEN_H) ? SCREEN_H : (y + h);
#ifdef __EMSCRIPTEN__
    for (int py = y1; py < y2; py++) {
        Uint32 *row = &wasm_fb[py * SCREEN_W];
        for (int px = x1; px < x2; px++) {
            row[px] = color;
        }
    }
#else
    SDL_Rect r;
    r.x = (Sint16)x1;
    r.y = (Sint16)y1;
    r.w = (Uint16)(x2 - x1);
    r.h = (Uint16)(y2 - y1);
    SDL_FillRect(screen, &r, color);
#endif
}

static Uint32 rgb(int r, int g, int b) {
#ifdef __EMSCRIPTEN__
    return 0xFF000000u | ((Uint32)(b & 0xFF) << 16) | ((Uint32)(g & 0xFF) << 8) | (Uint32)(r & 0xFF);
#else
    return SDL_MapRGB(screen->format, (Uint8)r, (Uint8)g, (Uint8)b);
#endif
}

/* Built-in 5x7 Pixel Font */
static const unsigned char font5x7[96][7] = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!' - 32] = {0x04,0x04,0x04,0x04,0x00,0x04,0x00},
    ['&' - 32] = {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},
    ['(' - 32] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    [')' - 32] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    ['+' - 32] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    ['-' - 32] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['.' - 32] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    ['/' - 32] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    ['0' - 32] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1' - 32] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2' - 32] = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['3' - 32] = {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E},
    ['4' - 32] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5' - 32] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6' - 32] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7' - 32] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8' - 32] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9' - 32] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    [':' - 32] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    ['?' - 32] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    ['A' - 32] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B' - 32] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C' - 32] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D' - 32] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    ['E' - 32] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F' - 32] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G' - 32] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    ['H' - 32] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I' - 32] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J' - 32] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    ['K' - 32] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L' - 32] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M' - 32] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N' - 32] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O' - 32] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P' - 32] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q' - 32] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R' - 32] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S' - 32] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['T' - 32] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U' - 32] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V' - 32] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W' - 32] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    ['X' - 32] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y' - 32] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z' - 32] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
};

static void draw_text(int x, int y, const char *str, Uint32 color, int scale) {
    for (int i = 0; str[i]; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 127) continue;
        const unsigned char *glyph = font5x7[c - 32];
        for (int row = 0; row < 7; row++) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (1 << (4 - col))) {
                    draw_rect(x + i * 6 * scale + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

static void add_score(int pts, float x, float y) {
    score += pts;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) {
            particles[i].active = 1;
            particles[i].x = x;
            particles[i].y = y;
            particles[i].vx = 0;
            particles[i].vy = -0.7f;
            particles[i].timer = 35;
            particles[i].type = 2;
            particles[i].value = pts;
            particles[i].color = rgb(255, 255, 255);
            break;
        }
    }
}

static void add_coin(float x, float y) {
    coins++;
    score += 200;
    if (coins >= 100) {
        coins -= 100;
        lives++;
        play_sfx(SFX_ONEUP);
    } else {
        play_sfx(SFX_COIN);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) {
            particles[i].active = 1;
            particles[i].x = x + 4;
            particles[i].y = y - 8;
            particles[i].vx = 0;
            particles[i].vy = -3.2f;
            particles[i].timer = 24;
            particles[i].type = 1;
            particles[i].value = 0;
            particles[i].color = rgb(255, 215, 0);
            break;
        }
    }
}

static void spawn_brick_debris(float x, float y) {
    float vx_list[4] = {-1.8f, 1.8f, -1.2f, 1.2f};
    float vy_list[4] = {-4.2f, -4.2f, -2.6f, -2.6f};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) {
                particles[i].active = 1;
                particles[i].x = x + (k & 1 ? 8 : 0);
                particles[i].y = y + (k >= 2 ? 8 : 0);
                particles[i].vx = vx_list[k];
                particles[i].vy = vy_list[k];
                particles[i].timer = 40;
                particles[i].type = 0;
                particles[i].color = (current_level == 1) ? rgb(60, 140, 220) : rgb(200, 76, 12);
                break;
            }
        }
    }
}

static void spawn_entity(int type, float x, float y, int dir) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!entities[i].active) {
            memset(&entities[i], 0, sizeof(Entity));
            entities[i].active = 1;
            entities[i].type = type;
            entities[i].x = x;
            entities[i].y = y;
            entities[i].start_x = x;
            entities[i].start_y = y;
            entities[i].dir = dir;
            entities[i].w = 14;
            entities[i].h = 14;
            if (type == ENT_WALKER) {
                entities[i].vx = 0.65f * dir;
            } else if (type == ENT_TURTLE) {
                entities[i].vx = 0.65f * dir;
                entities[i].h = 20;
                entities[i].y -= 6;
            } else if (type == ENT_PLANT) {
                entities[i].w = 14;
                entities[i].h = 20;
                entities[i].timer = 0;
            } else if (type == ENT_BOSS) {
                entities[i].w = 28;
                entities[i].h = 30;
                entities[i].hp = (current_level == 11) ? 10 : ((current_level == 7) ? 7 : 5);
                entities[i].vx = (current_level == 11) ? -0.75f : ((current_level == 7) ? -0.62f : -0.5f);
            } else if (type == ENT_MUSHROOM || type == ENT_ONEUP) {
                entities[i].vx = 1.0f;
                entities[i].vy = -1.5f;
            } else if (type == ENT_STAR) {
                entities[i].vx = 1.2f;
                entities[i].vy = -3.0f;
            } else if (type == ENT_PLATFORM) {
                entities[i].w = 48;
                entities[i].h = 8;
                entities[i].vy = (dir > 0) ? 0.8f : -0.8f;
            } else if (type == ENT_HPLATFORM) {
                entities[i].w = 48;
                entities[i].h = 8;
                entities[i].vx = (dir > 0) ? 0.9f : -0.9f;
            } else if (type == ENT_FLY_TURTLE) {
                entities[i].vx = 0.65f * dir;
                entities[i].h = 20;
                entities[i].y -= 6;
            }
            break;
        }
    }
}

/* Procedural Level Builder for Worlds 1-1, 1-2, 1-3, 1-4 and Bonus Room */
static void put_pipe(int x, int ground_y, int height, char top_left_char) {
    for (int r = ground_y - height; r < ground_y; r++) {
        if (r < 0 || r >= MAP_H) continue;
        if (r == ground_y - height) {
            map[r][x] = top_left_char; /* 'P' normal pipe, 'E' enterable bonus pipe */
            map[r][x + 1] = 'p';
        } else {
            map[r][x] = '[';
            map[r][x + 1] = ']';
        }
    }
}

static void build_level(int lvl, int bonus) {
    memset(map, ' ', sizeof(map));
    memset(bump_offset, 0, sizeof(bump_offset));
    memset(bump_orig, 0, sizeof(bump_orig));
    memset(entities, 0, sizeof(entities));
    memset(particles, 0, sizeof(particles));
    memset(fireballs, 0, sizeof(fireballs));
    boss_bridge_collapse = 0;

    for (int r = 0; r < MAP_H; r++) map[r][MAP_W] = '\0';

    if (bonus) {
        /* Secret Underground Coin Vault (accessed via pipe in 1-1) */
        for (int c = 0; c < 24; c++) {
            map[0][c] = 'B';
            map[13][c] = '#';
            map[14][c] = '#';
        }
        for (int r = 0; r < 13; r++) {
            map[r][0] = 'B';
            map[r][1] = 'B';
            map[r][22] = 'B';
            map[r][23] = 'B';
        }
        for (int c = 5; c <= 15; c++) {
            map[10][c] = 'B';
            map[8][c] = 'o';
            map[6][c] = 'o';
        }
        map[5][9] = '1'; /* Hidden 1-Up block */
        map[5][10] = '?';
        map[5][11] = '?';
        put_pipe(19, 13, 2, 'X'); /* Exit pipe back to surface */
        return;
    }

    if (lvl == 0) {
        /* ================= WORLD 1-1: SUNNY OVERWORLD ================= */
        for (int c = 0; c < MAP_W; c++) {
            if ((c >= 68 && c <= 70) || (c >= 86 && c <= 88) || (c >= 152 && c <= 154)) continue;
            map[13][c] = '#';
            map[14][c] = '#';
        }

        /* Intro blocks & first Super Mushroom */
        map[9][16] = '?';
        map[9][20] = 'B';
        map[9][21] = 'M'; /* Mushroom / Fire Flower */
        map[9][22] = 'B';
        map[9][23] = '?';
        map[9][24] = 'B';
        map[5][22] = '?';

        /* Coins in air */
        map[8][28] = 'o'; map[8][29] = 'o'; map[8][30] = 'o';

        /* Pipes (3rd pipe is enterable bonus room!) */
        put_pipe(32, 13, 2, 'P');
        put_pipe(42, 13, 3, 'P');
        spawn_entity(ENT_PLANT, 42 * TILE_SIZE + 9, (13 - 3) * TILE_SIZE - 16, 1);
        put_pipe(52, 13, 4, 'E'); /* Enterable Bonus Pipe! Press Down on this pipe! */
        put_pipe(62, 13, 2, 'P'); /* Return pipe from Bonus Room */

        /* Hidden 1-Up & Starman section */
        map[8][64] = '1';
        map[9][76] = 'B';
        map[9][77] = 'M';
        map[9][78] = 'B';
        for (int c = 79; c <= 84; c++) map[5][c] = 'B';
        for (int c = 80; c <= 83; c++) map[4][c] = 'o';

        /* Mid-level Checkpoint Flag at column 94 */
        map[12][94] = 'C';

        /* Starman block + enemy bowling */
        map[9][98] = 'B';
        map[9][99] = '*'; /* Starman! */
        map[9][105] = '?';
        map[9][108] = 'M';
        map[5][108] = '?';
        map[9][111] = '?';

        /* Brick pyramid & staircases */
        for (int h = 0; h < 4; h++) {
            for (int c = 132 + h; c <= 135; c++) map[12 - h][c] = 'S';
            for (int c = 138; c <= 141 - h; c++) map[12 - h][c] = 'S';
        }
        put_pipe(160, 13, 2, 'P');
        map[9][166] = 'B'; map[9][167] = 'B'; map[9][168] = '?'; map[9][169] = 'B';

        /* Final staircase to Flagpole */
        for (int h = 0; h < 8; h++) {
            for (int c = 178 + h; c <= 185; c++) map[12 - h][c] = 'S';
        }
        map[12][186] = 'S';

        /* Flagpole at 194, Castle at 200 */
        for (int r = 3; r <= 12; r++) map[r][194] = '|';
        map[2][194] = 'F';
        map[12][200] = 'K';

        /* Enemies */
        spawn_entity(ENT_WALKER, 22 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 40 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 50 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 51.5f * 16, 12 * 16, -1);
        spawn_entity(ENT_TURTLE, 80 * 16, 11 * 16, -1);
        spawn_entity(ENT_WALKER, 102 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 104 * 16, 12 * 16, -1);
        spawn_entity(ENT_TURTLE, 116 * 16, 11 * 16, -1);
        spawn_entity(ENT_WALKER, 124 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 126 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 170 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 172 * 16, 12 * 16, -1);

    } else if (lvl == 1) {
        /* ================= WORLD 1-2: UNDERGROUND CAVERN ================= */
        for (int c = 0; c < MAP_W; c++) {
            if (c >= 6 && c <= 180) map[1][c] = 'B';
            if ((c >= 56 && c <= 58) || (c >= 98 && c <= 101) || (c >= 142 && c <= 146)) continue;
            map[13][c] = '#';
            map[14][c] = '#';
        }
        map[9][12] = 'M';
        map[9][13] = '?';
        map[9][14] = '?';
        map[9][15] = '?';
        map[9][16] = '?';

        /* Rising brick pillars */
        for (int i = 0; i < 5; i++) {
            int col = 24 + i * 4;
            int top = 11 - (i < 3 ? i : 4 - i);
            for (int r = top; r <= 12; r++) map[r][col] = 'S';
            map[top - 2][col] = 'o';
        }

        /* Elevated brick chambers with coins */
        for (int c = 44; c <= 52; c++) {
            map[8][c] = 'B';
            map[6][c] = 'o';
        }
        map[8][48] = '*';

        /* Moving platform over gap 56..58 */
        spawn_entity(ENT_PLATFORM, 56 * 16, 10 * 16, 1);

        put_pipe(70, 13, 3, 'P');
        spawn_entity(ENT_PLANT, 70 * 16 + 9, 10 * 16 - 16, 1);
        put_pipe(78, 13, 4, 'P');
        spawn_entity(ENT_PLANT, 78 * 16 + 9, 9 * 16 - 16, 1);
        put_pipe(86, 13, 2, 'P');

        /* Checkpoint at column 92 */
        map[12][92] = 'C';
        map[9][94] = 'M';

        /* Moving platform over gap 98..101 */
        spawn_entity(ENT_PLATFORM, 98.5f * 16, 9 * 16, -1);

        for (int c = 110; c <= 122; c++) {
            map[9][c] = 'B';
            if (c % 2 == 0) map[5][c] = 'B';
            map[7][c] = 'o';
        }
        map[5][116] = '1';

        /* Moving platform over gap 142..146 */
        spawn_entity(ENT_PLATFORM, 143 * 16, 10 * 16, 1);

        put_pipe(156, 13, 3, 'P');
        put_pipe(164, 13, 2, 'P');

        for (int h = 0; h < 7; h++) {
            for (int c = 176 + h; c <= 182; c++) map[12 - h][c] = 'S';
        }
        for (int r = 3; r <= 12; r++) map[r][192] = '|';
        map[2][192] = 'F';
        map[12][198] = 'K';

        spawn_entity(ENT_WALKER, 20 * 16, 12 * 16, -1);
        spawn_entity(ENT_TURTLE, 35 * 16, 11 * 16, -1);
        spawn_entity(ENT_WALKER, 48 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 50 * 16, 12 * 16, -1);
        spawn_entity(ENT_TURTLE, 66 * 16, 11 * 16, -1);
        spawn_entity(ENT_WALKER, 114 * 16, 12 * 16, -1);
        spawn_entity(ENT_WALKER, 118 * 16, 8 * 16, -1);
        spawn_entity(ENT_TURTLE, 132 * 16, 11 * 16, -1);
        spawn_entity(ENT_WALKER, 168 * 16, 12 * 16, -1);

    } else if (lvl == 2) {
        /* ================= WORLD 1-3: SKY ATHLETIC BRIDGES ================= */
        /* Starting ground island */
        for (int c = 0; c <= 16; c++) { map[13][c] = '#'; map[14][c] = '#'; }

        /* Treetop bridges */
        for (int c = 19; c <= 32; c++) { map[10][c] = '='; map[8][c] = (c % 2 == 0) ? 'o' : ' '; }
        map[6][24] = '?'; map[6][25] = 'M'; map[6][26] = '?';

        for (int c = 36; c <= 48; c++) map[8][c] = '=';
        spawn_entity(ENT_PLATFORM, 51 * 16, 9 * 16, 1);

        for (int c = 56; c <= 72; c++) { map[11][c] = '='; }
        for (int c = 60; c <= 68; c++) { map[7][c] = '='; map[5][c] = 'o'; }

        spawn_entity(ENT_PLATFORM, 75 * 16, 8 * 16, -1);

        /* Mid-level Checkpoint island */
        for (int c = 80; c <= 94; c++) { map[12][c] = '#'; map[13][c] = '#'; map[14][c] = '#'; }
        map[11][84] = 'C';
        map[8][88] = 'M';
        map[8][89] = '?';

        for (int c = 98; c <= 114; c++) map[9][c] = '=';
        for (int c = 102; c <= 110; c++) map[6][c] = 'o';

        spawn_entity(ENT_PLATFORM, 117 * 16, 9 * 16, 1);
        for (int c = 123; c <= 138; c++) map[8][c] = '=';
        map[4][130] = '*';

        for (int c = 142; c <= 158; c++) { map[11][c] = '='; map[9][c] = (c % 2) ? 'o' : ' '; }
        spawn_entity(ENT_PLATFORM, 161 * 16, 10 * 16, -1);

        /* Goal island */
        for (int c = 167; c < MAP_W; c++) { map[13][c] = '#'; map[14][c] = '#'; }
        for (int h = 0; h < 7; h++) {
            for (int c = 172 + h; c <= 178; c++) map[12 - h][c] = 'S';
        }
        for (int r = 3; r <= 12; r++) map[r][188] = '|';
        map[2][188] = 'F';
        map[12][194] = 'K';

        spawn_entity(ENT_TURTLE, 26 * 16, 8 * 16, -1);
        spawn_entity(ENT_WALKER, 42 * 16, 7 * 16, -1);
        spawn_entity(ENT_TURTLE, 64 * 16, 9 * 16, -1);
        spawn_entity(ENT_WALKER, 106 * 16, 8 * 16, -1);
        spawn_entity(ENT_TURTLE, 110 * 16, 7 * 16, -1);
        spawn_entity(ENT_WALKER, 132 * 16, 7 * 16, -1);
        spawn_entity(ENT_TURTLE, 150 * 16, 9 * 16, -1);

    } else if (lvl == 3) {
        /* ================= WORLD 1-4: LAVA CASTLE & BOSS ================= */
        for (int c = 0; c < MAP_W; c++) {
            map[0][c] = 'S';
            map[1][c] = 'S';
            /* Lava pits */
            if ((c >= 24 && c <= 27) || (c >= 46 && c <= 50) || (c >= 76 && c <= 80) ||
                (c >= 112 && c <= 116) || (c >= 152 && c <= 168)) {
                map[13][c] = 'L';
                map[14][c] = 'L';
            } else {
                map[13][c] = 'S';
                map[14][c] = 'S';
            }
        }
        map[9][14] = 'M';
        map[9][34] = 'F'; /* Firebar pivot */
        map[10][52] = 'o'; map[10][53] = 'o'; map[10][54] = 'o';
        map[9][62] = 'F'; /* Firebar pivot */

        /* Checkpoint at column 86 */
        map[12][86] = 'C';
        map[9][90] = 'M';
        map[9][92] = '?';

        map[9][102] = 'F'; /* Firebar pivot */
        spawn_entity(ENT_PLATFORM, 113 * 16, 10 * 16, 1);

        for (int c = 124; c <= 136; c += 3) map[9][c] = '?';
        map[9][130] = 'M';

        /* Castle Boss Bridge over Lava (columns 152..166) */
        for (int c = 152; c <= 166; c++) {
            map[10][c] = '=';
        }
        /* Upper blocks above bridge to help jump over Boss */
        for (int c = 155; c <= 161; c++) {
            map[6][c] = 'S';
        }
        /* Golden Axe at end of bridge (col 167, row 9) */
        map[9][167] = 'A';
        map[10][167] = 'S';
        map[11][167] = 'S';
        map[12][167] = 'S';

        /* Spawn Castle Boss on bridge */
        spawn_entity(ENT_BOSS, 160 * 16, 8 * 16, -1);

    } else if (lvl == 4) {
        /* ================= WORLD 2-1: GOLDEN PYRAMID DESERT ================= */
        for (int c = 0; c < MAP_W; c++) {
            if ((c >= 66 && c <= 69) || (c >= 110 && c <= 114) || (c >= 148 && c <= 151)) continue;
            map[13][c] = '#';
            map[14][c] = '#';
        }
        map[9][14] = '?'; map[9][15] = 'M'; map[9][16] = '?';
        /* First Desert Springboard ('J') launching into high coin arc */
        map[12][22] = 'J';
        for (int c = 24; c <= 30; c++) map[5][c] = 'o';

        /* Sand pyramid 1 */
        for (int h = 0; h < 4; h++) {
            for (int c = 34 + h; c <= 41 - h; c++) map[12 - h][c] = 'S';
        }
        map[6][37] = '?'; map[6][38] = '*';

        /* Secret Oasis Bonus Pipe at 52, return at 62 */
        put_pipe(52, 13, 4, 'E');
        put_pipe(62, 13, 2, 'P');
        map[12][65] = 'J'; /* Springboard over desert chasm 66..69 */

        for (int c = 74; c <= 82; c++) { map[8][c] = 'B'; map[6][c] = 'o'; }
        map[8][78] = 'M';
        map[8][86] = '1'; /* Hidden 1-Up */

        /* Checkpoint at 94 */
        map[12][94] = 'C';
        map[9][98] = '?'; map[9][100] = 'M';

        /* Horizontal moving platform over wide quicksand chasm 110..114 */
        spawn_entity(ENT_HPLATFORM, 110 * 16, 11 * 16, 1);

        /* Twin desert pyramids */
        for (int h = 0; h < 4; h++) {
            for (int c = 122 + h; c <= 129 - h; c++) map[12 - h][c] = 'S';
            for (int c = 134 + h; c <= 141 - h; c++) map[12 - h][c] = 'S';
        }
        map[12][147] = 'J'; /* Springboard over chasm 148..151 */

        for (int c = 156; c <= 164; c += 2) { map[9][c] = '?'; map[7][c] = 'o'; }

        /* Grand Pyramid Staircase to Flagpole */
        for (int h = 0; h < 8; h++) {
            for (int c = 176 + h; c <= 183; c++) map[12 - h][c] = 'S';
        }
        for (int r = 3; r <= 12; r++) map[r][192] = '|';
        map[2][192] = 'F';
        map[12][198] = 'K';

        spawn_entity(ENT_WALKER, 20 * 16, 12 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 32 * 16, 10 * 16, -1);
        spawn_entity(ENT_TURTLE, 46 * 16, 11 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 78 * 16, 10 * 16, -1);
        spawn_entity(ENT_WALKER, 88 * 16, 12 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 104 * 16, 9 * 16, -1);
        spawn_entity(ENT_TURTLE, 132 * 16, 11 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 160 * 16, 9 * 16, -1);
        spawn_entity(ENT_WALKER, 168 * 16, 12 * 16, -1);

    } else if (lvl == 5) {
        /* ================= WORLD 2-2: CRYSTAL ICE CAVERN ================= */
        for (int c = 0; c < MAP_W; c++) {
            if (c >= 6 && c <= 180) map[1][c] = 'I';
            if ((c >= 50 && c <= 54) || (c >= 100 && c <= 105) || (c >= 144 && c <= 149)) continue;
            map[13][c] = (c >= 14 && c <= 170) ? 'I' : '#';
            map[14][c] = '#';
        }
        map[9][14] = 'M'; map[9][16] = '?'; map[9][18] = '?';

        /* Slippery Ice decks & springboards */
        for (int c = 24; c <= 36; c++) { map[9][c] = 'I'; map[7][c] = 'o'; }
        map[12][42] = 'J';
        for (int c = 44; c <= 48; c++) map[5][c] = 'I';

        spawn_entity(ENT_HPLATFORM, 50 * 16, 10 * 16, 1);

        put_pipe(64, 13, 3, 'P');
        spawn_entity(ENT_PLANT, 64 * 16 + 9, 10 * 16 - 16, 1);
        put_pipe(74, 13, 4, 'P');
        spawn_entity(ENT_PLANT, 74 * 16 + 9, 9 * 16 - 16, 1);

        map[8][82] = '*'; /* Starman on ice! */
        map[12][92] = 'C'; /* Checkpoint */
        map[9][95] = 'M';

        spawn_entity(ENT_PLATFORM, 101 * 16, 9 * 16, -1);

        for (int c = 112; c <= 128; c++) {
            map[9][c] = 'I';
            if (c % 2 == 0) map[5][c] = 'B';
            map[7][c] = 'o';
        }
        map[5][120] = '1';

        spawn_entity(ENT_HPLATFORM, 144 * 16, 10 * 16, 1);
        map[12][156] = 'J';
        for (int c = 158; c <= 166; c++) { map[6][c] = 'I'; map[4][c] = 'o'; }

        for (int h = 0; h < 7; h++) {
            for (int c = 176 + h; c <= 182; c++) map[12 - h][c] = 'I';
        }
        for (int r = 3; r <= 12; r++) map[r][192] = '|';
        map[2][192] = 'F';
        map[12][198] = 'K';

        spawn_entity(ENT_WALKER, 22 * 16, 12 * 16, -1);
        spawn_entity(ENT_TURTLE, 32 * 16, 7 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 60 * 16, 10 * 16, -1);
        spawn_entity(ENT_WALKER, 86 * 16, 12 * 16, -1);
        spawn_entity(ENT_TURTLE, 116 * 16, 7 * 16, -1);
        spawn_entity(ENT_WALKER, 124 * 16, 12 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 136 * 16, 9 * 16, -1);
        spawn_entity(ENT_TURTLE, 164 * 16, 11 * 16, -1);

    } else if (lvl == 6) {
        /* ================= WORLD 2-3: SNOWY CLOUD PEAKS ================= */
        for (int c = 0; c <= 16; c++) { map[13][c] = 'I'; map[14][c] = '#'; }
        map[12][15] = 'J';

        for (int c = 20; c <= 34; c++) { map[9][c] = 'I'; map[7][c] = (c % 2 == 0) ? 'o' : ' '; }
        map[5][26] = 'M'; map[5][27] = '?';

        spawn_entity(ENT_HPLATFORM, 38 * 16, 9 * 16, 1);

        for (int c = 48; c <= 64; c++) map[10][c] = '=';
        map[9][62] = 'J';
        for (int c = 66; c <= 76; c++) { map[5][c] = 'I'; map[3][c] = 'o'; }

        /* Checkpoint island */
        for (int c = 80; c <= 94; c++) { map[12][c] = 'I'; map[13][c] = '#'; map[14][c] = '#'; }
        map[11][84] = 'C';
        map[8][88] = 'M'; map[8][90] = '*';

        spawn_entity(ENT_HPLATFORM, 97 * 16, 10 * 16, 1);
        for (int c = 108; c <= 122; c++) { map[8][c] = 'I'; map[6][c] = 'o'; }
        spawn_entity(ENT_PLATFORM, 126 * 16, 9 * 16, -1);

        for (int c = 132; c <= 148; c++) map[10][c] = '=';
        map[9][146] = 'J';
        for (int c = 150; c <= 162; c++) { map[6][c] = 'I'; map[4][c] = 'o'; }

        for (int c = 166; c < MAP_W; c++) { map[13][c] = 'I'; map[14][c] = '#'; }
        for (int h = 0; h < 7; h++) {
            for (int c = 172 + h; c <= 178; c++) map[12 - h][c] = 'I';
        }
        for (int r = 3; r <= 12; r++) map[r][188] = '|';
        map[2][188] = 'F';
        map[12][194] = 'K';

        spawn_entity(ENT_FLY_TURTLE, 28 * 16, 6 * 16, -1);
        spawn_entity(ENT_TURTLE, 54 * 16, 8 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 70 * 16, 3 * 16, -1);
        spawn_entity(ENT_WALKER, 114 * 16, 7 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 138 * 16, 7 * 16, -1);
        spawn_entity(ENT_TURTLE, 156 * 16, 4 * 16, -1);

    } else if (lvl == 7) {
        /* ================= WORLD 2-4: FROST-FIRE FORTRESS & BOSS ================= */
        for (int c = 0; c < MAP_W; c++) {
            map[0][c] = 'S';
            map[1][c] = 'S';
            if ((c >= 22 && c <= 26) || (c >= 44 && c <= 50) || (c >= 74 && c <= 80) ||
                (c >= 110 && c <= 118) || (c >= 152 && c <= 168)) {
                map[13][c] = 'L';
                map[14][c] = 'L';
            } else {
                map[13][c] = (c >= 52 && c <= 108) ? 'I' : 'S';
                map[14][c] = 'S';
            }
        }
        map[9][14] = 'M';
        map[12][21] = 'J';
        map[9][34] = 'F';
        spawn_entity(ENT_HPLATFORM, 45 * 16, 10 * 16, 1);
        map[9][62] = 'F';
        spawn_entity(ENT_PLATFORM, 75 * 16, 9 * 16, -1);

        map[12][86] = 'C';
        map[9][90] = 'M'; map[9][92] = '?';
        map[9][102] = 'F';
        spawn_entity(ENT_HPLATFORM, 111 * 16, 10 * 16, 1);

        for (int c = 124; c <= 136; c += 3) map[9][c] = '?';
        map[9][130] = 'M';
        map[12][142] = 'J';

        for (int c = 152; c <= 166; c++) map[10][c] = '=';
        for (int c = 155; c <= 161; c++) map[6][c] = 'I';
        map[9][167] = 'A';
        map[10][167] = 'S'; map[11][167] = 'S'; map[12][167] = 'S';

        spawn_entity(ENT_FLY_TURTLE, 38 * 16, 9 * 16, -1);
        spawn_entity(ENT_TURTLE, 68 * 16, 11 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 128 * 16, 8 * 16, -1);
        spawn_entity(ENT_BOSS, 160 * 16, 8 * 16, -1);

    } else if (lvl == 8) {
        /* ================= WORLD 3-1: STARLIGHT NIGHT OVERWORLD ================= */
        for (int c = 0; c < MAP_W; c++) {
            if ((c >= 66 && c <= 70) || (c >= 108 && c <= 113) || (c >= 148 && c <= 152)) continue;
            map[13][c] = '#';
            map[14][c] = '#';
        }
        map[9][15] = 'M'; map[9][16] = '?'; map[9][17] = '?';
        put_pipe(26, 13, 3, 'P');
        spawn_entity(ENT_PLANT, 26 * 16 + 9, 10 * 16 - 16, 1);
        map[12][34] = 'J';
        for (int c = 36; c <= 46; c++) { map[6][c] = '='; map[4][c] = 'o'; }

        /* Secret Starlight Vault Pipe at 52, return at 62 */
        put_pipe(52, 13, 4, 'E');
        put_pipe(62, 13, 2, 'P');
        spawn_entity(ENT_HPLATFORM, 66 * 16, 10 * 16, 1);

        for (int c = 76; c <= 86; c++) { map[8][c] = 'B'; map[6][c] = 'o'; }
        map[8][81] = '*';
        map[12][94] = 'C'; /* Checkpoint */
        map[9][98] = 'M'; map[5][98] = '1';

        spawn_entity(ENT_HPLATFORM, 108 * 16, 9 * 16, 1);
        put_pipe(120, 13, 4, 'P');
        spawn_entity(ENT_PLANT, 120 * 16 + 9, 9 * 16 - 16, 1);
        put_pipe(130, 13, 3, 'P');
        spawn_entity(ENT_PLANT, 130 * 16 + 9, 10 * 16 - 16, 1);

        map[12][146] = 'J';
        for (int c = 154; c <= 166; c++) { map[7][c] = '='; map[5][c] = 'o'; }

        for (int h = 0; h < 8; h++) {
            for (int c = 176 + h; c <= 183; c++) map[12 - h][c] = 'S';
        }
        for (int r = 3; r <= 12; r++) map[r][192] = '|';
        map[2][192] = 'F';
        map[12][198] = 'K';

        spawn_entity(ENT_FLY_TURTLE, 22 * 16, 9 * 16, -1);
        spawn_entity(ENT_TURTLE, 42 * 16, 4 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 78 * 16, 10 * 16, -1);
        spawn_entity(ENT_WALKER, 84 * 16, 12 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 102 * 16, 8 * 16, -1);
        spawn_entity(ENT_TURTLE, 138 * 16, 11 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 160 * 16, 5 * 16, -1);
        spawn_entity(ENT_WALKER, 170 * 16, 12 * 16, -1);

    } else if (lvl == 9) {
        /* ================= WORLD 3-2: HAUNTED SHADOW CAVERN ================= */
        for (int c = 0; c < MAP_W; c++) {
            if (c >= 6 && c <= 180) map[1][c] = 'B';
            if ((c >= 44 && c <= 49) || (c >= 98 && c <= 104) || (c >= 140 && c <= 146)) continue;
            map[13][c] = '#';
            map[14][c] = '#';
        }
        map[9][14] = 'M'; map[9][16] = '?';
        map[9][28] = 'F'; /* Underground Firebar! */
        map[12][42] = 'J';
        spawn_entity(ENT_HPLATFORM, 44 * 16, 8 * 16, 1);

        for (int c = 56; c <= 68; c++) { map[8][c] = 'B'; map[6][c] = 'o'; }
        map[8][62] = '*';
        map[9][76] = 'F';

        map[12][92] = 'C'; /* Checkpoint */
        map[9][95] = 'M';
        spawn_entity(ENT_PLATFORM, 99 * 16, 9 * 16, 1);

        for (int c = 110; c <= 126; c++) {
            map[9][c] = 'I';
            if (c % 2 == 0) map[5][c] = 'B';
            map[7][c] = 'o';
        }
        map[5][118] = '1';
        map[9][132] = 'F';
        spawn_entity(ENT_HPLATFORM, 140 * 16, 10 * 16, -1);

        for (int h = 0; h < 7; h++) {
            for (int c = 176 + h; c <= 182; c++) map[12 - h][c] = 'S';
        }
        for (int r = 3; r <= 12; r++) map[r][192] = '|';
        map[2][192] = 'F';
        map[12][198] = 'K';

        spawn_entity(ENT_WALKER, 20 * 16, 12 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 36 * 16, 9 * 16, -1);
        spawn_entity(ENT_TURTLE, 62 * 16, 6 * 16, -1);
        spawn_entity(ENT_WALKER, 84 * 16, 12 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 114 * 16, 7 * 16, -1);
        spawn_entity(ENT_TURTLE, 122 * 16, 11 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 156 * 16, 9 * 16, -1);
        spawn_entity(ENT_WALKER, 166 * 16, 12 * 16, -1);

    } else if (lvl == 10) {
        /* ================= WORLD 3-3: SKY ARMADA ATHLETIC ================= */
        for (int c = 0; c <= 16; c++) { map[13][c] = '#'; map[14][c] = '#'; }
        map[12][14] = 'J';

        for (int c = 20; c <= 34; c++) { map[8][c] = '='; map[6][c] = 'o'; }
        map[4][27] = 'M';
        spawn_entity(ENT_HPLATFORM, 38 * 16, 8 * 16, 1);

        for (int c = 48; c <= 62; c++) map[10][c] = 'I';
        map[9][60] = 'J';
        for (int c = 64; c <= 76; c++) { map[5][c] = '='; map[3][c] = 'o'; }

        /* Checkpoint deck */
        for (int c = 80; c <= 94; c++) { map[12][c] = '#'; map[13][c] = '#'; map[14][c] = '#'; }
        map[11][84] = 'C';
        map[8][88] = 'M'; map[8][90] = '*';

        spawn_entity(ENT_HPLATFORM, 97 * 16, 9 * 16, 1);
        spawn_entity(ENT_PLATFORM, 110 * 16, 8 * 16, -1);

        for (int c = 118; c <= 134; c++) { map[9][c] = 'I'; map[7][c] = 'o'; }
        map[8][132] = 'J';
        for (int c = 138; c <= 152; c++) { map[5][c] = '='; map[3][c] = 'o'; }
        spawn_entity(ENT_HPLATFORM, 155 * 16, 9 * 16, 1);

        for (int c = 166; c < MAP_W; c++) { map[13][c] = '#'; map[14][c] = '#'; }
        for (int h = 0; h < 7; h++) {
            for (int c = 172 + h; c <= 178; c++) map[12 - h][c] = 'S';
        }
        for (int r = 3; r <= 12; r++) map[r][188] = '|';
        map[2][188] = 'F';
        map[12][194] = 'K';

        spawn_entity(ENT_FLY_TURTLE, 26 * 16, 5 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 54 * 16, 7 * 16, -1);
        spawn_entity(ENT_TURTLE, 70 * 16, 3 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 124 * 16, 6 * 16, -1);
        spawn_entity(ENT_TURTLE, 144 * 16, 3 * 16, -1);

    } else if (lvl == 11) {
        /* ================= WORLD 3-4: GRAND KOOPA-KING CITADEL ================= */
        for (int c = 0; c < MAP_W; c++) {
            map[0][c] = 'S';
            map[1][c] = 'S';
            if ((c >= 20 && c <= 26) || (c >= 42 && c <= 50) || (c >= 72 && c <= 80) ||
                (c >= 106 && c <= 118) || (c >= 152 && c <= 168)) {
                map[13][c] = 'L';
                map[14][c] = 'L';
            } else {
                map[13][c] = (c >= 82 && c <= 104) ? 'I' : 'S';
                map[14][c] = 'S';
            }
        }
        map[9][14] = 'M';
        map[12][19] = 'J';
        map[9][32] = 'F';
        spawn_entity(ENT_HPLATFORM, 43 * 16, 10 * 16, 1);
        map[9][58] = 'F';
        map[9][66] = 'F';
        spawn_entity(ENT_PLATFORM, 74 * 16, 9 * 16, 1);

        map[12][86] = 'C'; /* Checkpoint */
        map[9][90] = 'M'; map[9][92] = '?';
        map[9][100] = 'F';
        spawn_entity(ENT_HPLATFORM, 108 * 16, 10 * 16, 1);

        for (int c = 124; c <= 136; c += 3) map[9][c] = '?';
        map[9][130] = 'M';
        map[12][142] = 'J';

        /* Final Boss Bridge over Lava (columns 152..166) */
        for (int c = 152; c <= 166; c++) map[10][c] = '=';
        for (int c = 155; c <= 161; c++) map[6][c] = 'S';
        map[9][167] = 'A';
        map[10][167] = 'S'; map[11][167] = 'S'; map[12][167] = 'S';

        spawn_entity(ENT_FLY_TURTLE, 36 * 16, 9 * 16, -1);
        spawn_entity(ENT_TURTLE, 62 * 16, 11 * 16, -1);
        spawn_entity(ENT_FLY_TURTLE, 128 * 16, 8 * 16, -1);
        spawn_entity(ENT_BOSS, 160 * 16, 8 * 16, -1);
    }
}

static void reset_player_at_spawn(int keep_power) {
    int pwr = keep_power ? player.power : STATE_SMALL;
    memset(&player, 0, sizeof(player));
    player.power = pwr;
    player.w = 12;
    player.h = (player.power == STATE_SMALL) ? 14 : 26;
    player.dir = 1;
    if (checkpoint_reached) {
        player.x = checkpoint_x;
        player.y = checkpoint_y - player.h;
    } else {
        player.x = 40.0f;
        player.y = 13 * TILE_SIZE - player.h;
    }
    cam_x = player.x - 80.0f;
    if (cam_x < 0) cam_x = 0;
}

static void start_level(int lvl, int keep_power) {
    current_level = lvl;
    in_bonus_room = 0;
    level_timer = 400;
    build_level(lvl, 0);
    reset_player_at_spawn(keep_power);
}

static int is_solid_tile(char t) {
    return (t == '#' || t == 'B' || t == '?' || t == 'M' || t == '*' ||
            t == '1' || t == 'U' || t == 'S' || t == 'P' || t == 'p' ||
            t == 'E' || t == 'X' || t == '[' || t == ']' || t == '=' ||
            t == 'I' || t == 'J');
}

static char get_tile(int tx, int ty) {
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return ' ';
    return map[ty][tx];
}

/* Hit a block from underneath */
static void hit_block_below(int tx, int ty) {
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return;
    char t = map[ty][tx];
    if (t == '?' || t == 'M' || t == '*' || t == '1' || t == 'B') {
        bump_offset[ty][tx] = 8;
        bump_orig[ty][tx] = t;

        /* Check if any enemy is standing on top of this bumped block */
        float bx1 = tx * TILE_SIZE - 4;
        float bx2 = (tx + 1) * TILE_SIZE + 4;
        float by = ty * TILE_SIZE;
        for (int i = 0; i < MAX_ENTITIES; i++) {
            if (entities[i].active &&
                (entities[i].type == ENT_WALKER || entities[i].type == ENT_TURTLE || entities[i].type == ENT_SHELL)) {
                if (entities[i].x + entities[i].w > bx1 && entities[i].x < bx2 &&
                    fabsf((entities[i].y + entities[i].h) - by) < 8.0f) {
                    entities[i].active = 0;
                    add_score(200, entities[i].x, entities[i].y);
                    play_sfx(SFX_KICK);
                }
            }
        }

        if (t == '?') {
            map[ty][tx] = 'U';
            add_coin(tx * TILE_SIZE, ty * TILE_SIZE);
        } else if (t == 'M') {
            map[ty][tx] = 'U';
            play_sfx(SFX_POWERUP_APPEAR);
            if (player.power == STATE_SMALL) {
                spawn_entity(ENT_MUSHROOM, tx * TILE_SIZE, (ty - 1) * TILE_SIZE, 1);
            } else {
                spawn_entity(ENT_FLOWER, tx * TILE_SIZE, (ty - 1) * TILE_SIZE, 1);
            }
        } else if (t == '*') {
            map[ty][tx] = 'U';
            play_sfx(SFX_POWERUP_APPEAR);
            spawn_entity(ENT_STAR, tx * TILE_SIZE, (ty - 1) * TILE_SIZE, 1);
        } else if (t == '1') {
            map[ty][tx] = 'U';
            play_sfx(SFX_POWERUP_APPEAR);
            spawn_entity(ENT_ONEUP, tx * TILE_SIZE, (ty - 1) * TILE_SIZE, 1);
        } else if (t == 'B') {
            if (player.power != STATE_SMALL) {
                map[ty][tx] = ' ';
                bump_offset[ty][tx] = 0;
                play_sfx(SFX_BRICK);
                add_score(50, tx * TILE_SIZE, ty * TILE_SIZE);
                spawn_brick_debris(tx * TILE_SIZE, ty * TILE_SIZE);
            } else {
                play_sfx(SFX_BUMP);
            }
        }
    } else if (t == 'U' || t == 'S') {
        play_sfx(SFX_BUMP);
    }
}

static void hurt_player(void) {
    if (player.dead || player.invuln_timer > 0 || player.star_timer > 0 || player.flagpole_state > 0) return;
    if (player.power > STATE_SMALL) {
        player.power = STATE_SMALL;
        player.y += 12;
        player.h = 14;
        player.invuln_timer = 110;
        play_sfx(SFX_SHRINK);
    } else {
        player.dead = 1;
        player.dead_timer = 110;
        player.vy = -5.2f;
        player.vx = 0;
        play_sfx(SFX_DIE);
    }
}

static int rects_overlap(float x1, float y1, int w1, int h1, float x2, float y2, int w2, int h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

static void update_game(const Uint8 *keys) {
    frame_counter++;
    firebar_angle = (firebar_angle + 3) % 360;

    if (frame_counter % 36 == 0 && !player.dead && player.flagpole_state == 0 && !game_won) {
        if (level_timer > 0) level_timer--;
    }

    /* Update bumped blocks */
    for (int r = 0; r < MAP_H; r++) {
        for (int c = 0; c < MAP_W; c++) {
            if (bump_offset[r][c] > 0) bump_offset[r][c] -= 2;
        }
    }

    /* Update particles */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            if (particles[i].type == 0 || particles[i].type == 1) {
                particles[i].vy += 0.28f;
            }
            particles[i].timer--;
            if (particles[i].timer <= 0) particles[i].active = 0;
        }
    }

    if (game_won) return;

    /* Handle Player Death Animation */
    if (player.dead) {
        player.y += player.vy;
        player.vy += 0.22f;
        player.dead_timer--;
        if (player.dead_timer <= 0) {
            lives--;
            if (lives <= 0) {
                /* Unlimited continues on current level! */
                lives = 5;
                checkpoint_reached = 0;
            }
            start_level(current_level, 0);
        }
        return;
    }

    /* Handle Flagpole / Stage Clear Sequence */
    if (player.flagpole_state == 1) {
        player.y += 2.2f;
        if (player.y + player.h >= 13 * TILE_SIZE) {
            player.y = 13 * TILE_SIZE - player.h;
            player.flagpole_state = 2;
            player.flagpole_timer = 110;
            play_sfx(SFX_STAGE_CLEAR);
        }
        return;
    } else if (player.flagpole_state == 2) {
        player.x += 1.2f;
        player.dir = 1;
        player.anim_frame += 0.2f;
        player.flagpole_timer--;
        if (player.flagpole_timer <= 0) {
            checkpoint_reached = 0;
            if (current_level < 11) {
                start_level(current_level + 1, 1);
            } else {
                game_won = 1;
            }
        }
        return;
    }

    /* Handle Boss Bridge Collapse in 1-4, 2-4, 3-4 */
    if (boss_bridge_collapse > 0) {
        boss_bridge_collapse++;
        int col = 166 - (boss_bridge_collapse / 4);
        if (col >= 152 && col <= 166) {
            map[10][col] = ' ';
            play_sfx(SFX_BRICK);
        }
        for (int i = 0; i < MAX_ENTITIES; i++) {
            if (entities[i].active && entities[i].type == ENT_BOSS) {
                if (boss_bridge_collapse > 50) {
                    entities[i].y += 3.5f;
                    if (entities[i].y > SCREEN_H) entities[i].active = 0;
                }
            }
        }
        if (boss_bridge_collapse == 65) {
            play_sfx(SFX_STAGE_CLEAR);
        }
        if (boss_bridge_collapse > 160) {
            checkpoint_reached = 0;
            if (current_level < 11) {
                start_level(current_level + 1, 1);
            } else {
                game_won = 1;
            }
        }
        return;
    }

    if (player.invuln_timer > 0) player.invuln_timer--;
    if (player.star_timer > 0) player.star_timer--;
    if (player.fire_cooldown > 0) player.fire_cooldown--;

    /* Dual-mapped Keyboard Input */
    int left  = keys[SDLK_LEFT]  || keys[SDLK_a];
    int right = keys[SDLK_RIGHT] || keys[SDLK_d];
    int down  = keys[SDLK_DOWN]  || keys[SDLK_s];
    int jump  = keys[SDLK_SPACE] || keys[SDLK_x] || keys[SDLK_UP] || keys[SDLK_w];
    int run   = keys[SDLK_z]     || keys[SDLK_LSHIFT] || keys[SDLK_RSHIFT] || keys[SDLK_LCTRL];

    /* Crouching */
    if (player.on_ground && down && player.power != STATE_SMALL) {
        if (!player.crouch) {
            player.crouch = 1;
            player.y += 10;
            player.h = 16;
        }
    } else if (player.crouch) {
        player.crouch = 0;
        player.y -= 10;
        player.h = (player.power == STATE_SMALL) ? 14 : 26;
    }

    /* Check Enterable Bonus Pipe ('E' when pressing Down, or 'X' when walking right into exit pipe) */
    if (player.on_ground && down && !in_bonus_room) {
        int tx = (int)((player.x + player.w / 2) / TILE_SIZE);
        int ty = (int)((player.y + player.h + 2) / TILE_SIZE);
        if (get_tile(tx, ty) == 'E' || get_tile(tx - 1, ty) == 'E') {
            in_bonus_room = 1;
            player.crouch = 0;
            player.h = (player.power == STATE_SMALL) ? 14 : 26;
            return_x = 62 * TILE_SIZE + 4;
            return_y = 11 * TILE_SIZE - player.h;
            build_level(current_level, 1);
            player.x = 3 * TILE_SIZE;
            player.y = 3 * TILE_SIZE;
            player.vx = player.vy = 0;
            cam_x = 0;
            play_sfx(SFX_SHRINK);
            return;
        }
    }

    /* Fireball throwing */
    static int prev_run = 0;
    if (run && !prev_run && player.power == STATE_FIRE && player.fire_cooldown == 0 && !player.crouch) {
        for (int i = 0; i < MAX_FIREBALLS; i++) {
            if (!fireballs[i].active) {
                fireballs[i].active = 1;
                fireballs[i].x = player.dir > 0 ? (player.x + player.w) : (player.x - 6);
                fireballs[i].y = player.y + 8;
                fireballs[i].vx = 4.2f * player.dir;
                fireballs[i].vy = 1.5f;
                fireballs[i].timer = 140;
                player.fire_cooldown = 16;
                play_sfx(SFX_FIREBALL);
                break;
            }
        }
    }
    prev_run = run;

    /* Classic Super Mario Bros. Horizontal Momentum & Skidding Physics (+ Ice Friction!) */
    int foot_tx = (int)((player.x + player.w / 2) / TILE_SIZE);
    int foot_ty = (int)((player.y + player.h + 1) / TILE_SIZE);
    int on_ice = player.on_ground && (get_tile(foot_tx, foot_ty) == 'I');

    float max_speed = run ? 3.1f : 2.0f;
    float accel = player.on_ground ? (on_ice ? 0.065f : (run ? 0.14f : 0.10f)) : 0.08f;
    float decel = player.on_ground ? (on_ice ? 0.022f : 0.10f) : 0.03f;
    float skid_decel = on_ice ? 0.09f : 0.24f;

    player.skidding = 0;
    if (left && !player.crouch) {
        player.dir = -1;
        if (player.vx > 0.4f && player.on_ground) {
            player.vx -= skid_decel;
            player.skidding = 1;
        } else {
            player.vx -= accel;
            if (player.vx < -max_speed) player.vx = -max_speed;
        }
    } else if (right && !player.crouch) {
        player.dir = 1;
        if (player.vx < -0.4f && player.on_ground) {
            player.vx += skid_decel;
            player.skidding = 1;
        } else {
            player.vx += accel;
            if (player.vx > max_speed) player.vx = max_speed;
        }
    } else {
        if (player.vx > 0) {
            player.vx -= decel;
            if (player.vx < 0) player.vx = 0;
        } else if (player.vx < 0) {
            player.vx += decel;
            if (player.vx > 0) player.vx = 0;
        }
    }

    if (fabsf(player.vx) > 0.1f && player.on_ground) {
        player.anim_frame += fabsf(player.vx) * 0.12f;
    } else if (player.on_ground) {
        player.anim_frame = 0;
    }

    /* Variable-Height Jump Physics */
    static int prev_jump = 0;
    if (jump && !prev_jump && player.on_ground) {
        float speed_bonus = (fabsf(player.vx) > 2.3f) ? 0.55f : 0.0f;
        player.vy = -4.6f - speed_bonus;
        player.on_ground = 0;
        player.jump_hold = 16;
        play_sfx(SFX_JUMP);
    }
    if (jump && player.jump_hold > 0 && player.vy < 0) {
        player.vy -= 0.20f;
        player.jump_hold--;
    } else {
        player.jump_hold = 0;
    }
    prev_jump = jump;

    /* Gravity */
    player.vy += 0.27f;
    if (player.vy > 5.0f) player.vy = 5.0f;

    /* Horizontal Movement & Tile Collision */
    player.x += player.vx;
    if (player.x < cam_x) {
        player.x = cam_x;
        if (player.vx < 0) player.vx = 0;
    }

    int tx_left  = (int)(player.x / TILE_SIZE);
    int tx_right = (int)((player.x + player.w - 0.01f) / TILE_SIZE);
    int ty_top   = (int)(player.y / TILE_SIZE);
    int ty_bot   = (int)((player.y + player.h - 0.01f) / TILE_SIZE);

    for (int ty = ty_top; ty <= ty_bot; ty++) {
        for (int tx = tx_left; tx <= tx_right; tx++) {
            char t = get_tile(tx, ty);
            /* Exit pipe in bonus room */
            if (in_bonus_room && (t == 'X' || (t == '[' && get_tile(tx, ty - 1) == 'X'))) {
                in_bonus_room = 0;
                build_level(current_level, 0);
                player.x = return_x;
                player.y = return_y;
                player.vx = 0;
                player.vy = 0;
                cam_x = player.x - 100.0f;
                if (cam_x < 0) cam_x = 0;
                play_sfx(SFX_POWERUP_APPEAR);
                return;
            }
            if (is_solid_tile(t)) {
                if (player.vx > 0) {
                    player.x = tx * TILE_SIZE - player.w;
                    player.vx = 0;
                } else if (player.vx < 0) {
                    player.x = (tx + 1) * TILE_SIZE;
                    player.vx = 0;
                }
            }
        }
    }

    /* Vertical Movement & Tile Collision */
    float orig_vy = player.vy;
    player.y += player.vy;
    player.on_ground = 0;

    tx_left  = (int)(player.x / TILE_SIZE);
    tx_right = (int)((player.x + player.w - 0.01f) / TILE_SIZE);
    ty_top   = (int)(player.y / TILE_SIZE);
    ty_bot   = (int)((player.y + player.h - 0.01f) / TILE_SIZE);

    int resolved_solid_y = 0;
    for (int ty = ty_top; ty <= ty_bot; ty++) {
        for (int tx = tx_left; tx <= tx_right; tx++) {
            char t = get_tile(tx, ty);
            if (t == 'o') {
                map[ty][tx] = ' ';
                add_coin(tx * TILE_SIZE, ty * TILE_SIZE);
                continue;
            }
            if (t == 'C') {
                map[ty][tx] = 'c';
                checkpoint_reached = 1;
                checkpoint_x = tx * TILE_SIZE;
                checkpoint_y = (ty + 1) * TILE_SIZE;
                play_sfx(SFX_CHECKPOINT);
                add_score(500, tx * TILE_SIZE, ty * TILE_SIZE);
                continue;
            }
            if (t == '|' || (t == 'F' && ty <= 3)) {
                if ((current_level % 4) != 3 && player.flagpole_state == 0) {
                    player.flagpole_state = 1;
                    player.x = tx * TILE_SIZE - 6;
                    player.vx = player.vy = 0;
                    add_score(1000, player.x, player.y);
                    play_sfx(SFX_FLAGPOLE);
                    return;
                }
            }
            if (t == 'A') {
                /* Touched Golden Axe in Castle Boss stage! */
                map[ty][tx] = ' ';
                boss_bridge_collapse = 1;
                add_score(2000, tx * TILE_SIZE, ty * TILE_SIZE);
                play_sfx(SFX_FLAGPOLE);
                return;
            }
            if (t == 'L') {
                player.power = STATE_SMALL;
                player.invuln_timer = 0;
                player.star_timer = 0;
                hurt_player();
                return;
            }
            if (!resolved_solid_y && is_solid_tile(t)) {
                float prev_bottom = (player.y - orig_vy) + player.h;
                if (orig_vy > 0 && prev_bottom <= ty * TILE_SIZE + 6.0f) {
                    player.y = ty * TILE_SIZE - player.h;
                    if (t == 'J') {
                        /* Springboard super-bounce! */
                        player.vy = jump ? -7.2f : -5.6f;
                        player.on_ground = 0;
                        player.jump_hold = 14;
                        play_sfx(SFX_JUMP);
                    } else {
                        player.vy = 0;
                        player.on_ground = 1;
                    }
                    resolved_solid_y = 1;
                } else if (orig_vy < 0 && (player.y - orig_vy) >= (ty + 1) * TILE_SIZE - 6.0f) {
                    int center_tx = (int)((player.x + player.w / 2) / TILE_SIZE);
                    int hit_tx = is_solid_tile(get_tile(center_tx, ty)) ? center_tx : tx;
                    player.y = (ty + 1) * TILE_SIZE;
                    player.vy = 0.5f;
                    player.jump_hold = 0;
                    hit_block_below(hit_tx, ty);
                    resolved_solid_y = 1;
                }
            }
        }
    }

    /* Pit Fall Check */
    if (player.y > SCREEN_H + 16) {
        player.power = STATE_SMALL;
        player.invuln_timer = 0;
        player.star_timer = 0;
        hurt_player();
        return;
    }

    /* Update Camera (Smooth side-scrolling) */
    if (!in_bonus_room) {
        float target_cam = player.x - 110.0f;
        if (target_cam > cam_x) cam_x = target_cam;
        if (cam_x < 0) cam_x = 0;
        if (cam_x > (MAP_W * TILE_SIZE - SCREEN_W)) cam_x = (float)(MAP_W * TILE_SIZE - SCREEN_W);
    } else {
        cam_x = 0;
    }

    /* Check Rotating Firebars (row >= 4 so flagpole top 'F' at row 2 is ignored) */
    {
        float rad = firebar_angle * 3.14159265f / 180.0f;
        int start_c = (int)(cam_x / TILE_SIZE) - 2;
        int end_c   = start_c + (SCREEN_W / TILE_SIZE) + 4;
        if (start_c < 0) start_c = 0;
        if (end_c >= MAP_W) end_c = MAP_W - 1;
        for (int r = 4; r < MAP_H; r++) {
            for (int c = start_c; c <= end_c; c++) {
                if (map[r][c] == 'F') {
                    float cx = c * TILE_SIZE + 8;
                    float cy = r * TILE_SIZE + 8;
                    for (int k = 1; k <= 4; k++) {
                        float fx = cx + cosf(rad) * (k * 10) - 3;
                        float fy = cy + sinf(rad) * (k * 10) - 3;
                        if (rects_overlap(player.x, player.y, player.w, player.h, fx, fy, 6, 6)) {
                            hurt_player();
                        }
                    }
                }
            }
        }
    }

    /* Update Player Fireballs */
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        if (!fireballs[i].active) continue;
        fireballs[i].x += fireballs[i].vx;
        fireballs[i].y += fireballs[i].vy;
        fireballs[i].vy += 0.25f;
        fireballs[i].timer--;

        int ftx = (int)((fireballs[i].x + 3) / TILE_SIZE);
        int fty = (int)((fireballs[i].y + 6) / TILE_SIZE);
        if (is_solid_tile(get_tile(ftx, fty))) {
            fireballs[i].y = fty * TILE_SIZE - 6;
            fireballs[i].vy = -2.4f;
        }
        int ftx_side = (int)((fireballs[i].x + (fireballs[i].vx > 0 ? 6 : 0)) / TILE_SIZE);
        int fty_mid  = (int)((fireballs[i].y + 3) / TILE_SIZE);
        if (is_solid_tile(get_tile(ftx_side, fty_mid)) || fireballs[i].timer <= 0 ||
            fireballs[i].x < cam_x || fireballs[i].x > cam_x + SCREEN_W) {
            fireballs[i].active = 0;
            continue;
        }

        /* Check Fireball hitting Enemies */
        for (int e = 0; e < MAX_ENTITIES; e++) {
            if (!entities[e].active) continue;
            int et = entities[e].type;
            if (et == ENT_WALKER || et == ENT_TURTLE || et == ENT_FLY_TURTLE ||
                et == ENT_SHELL || et == ENT_PLANT || et == ENT_BOSS) {
                if (rects_overlap(fireballs[i].x, fireballs[i].y, 6, 6,
                                  entities[e].x, entities[e].y, entities[e].w, entities[e].h)) {
                    fireballs[i].active = 0;
                    if (et == ENT_BOSS) {
                        entities[e].hp--;
                        play_sfx(SFX_KICK);
                        if (entities[e].hp <= 0) {
                            entities[e].active = 0;
                            add_score(2000, entities[e].x, entities[e].y);
                            boss_bridge_collapse = 1;
                        }
                    } else {
                        entities[e].active = 0;
                        add_score(200, entities[e].x, entities[e].y);
                        play_sfx(SFX_KICK);
                    }
                    break;
                }
            }
        }
    }

    /* Update Entities */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (!e->active) continue;

        /* Only wake up enemies once camera approaches */
        if (e->x > cam_x + SCREEN_W + 64) continue;
        if (e->x < cam_x - 64 && e->type != ENT_PLATFORM && e->type != ENT_HPLATFORM) {
            e->active = 0;
            continue;
        }

        if (e->type == ENT_PLATFORM) {
            e->y += e->vy;
            if (e->y < 4 * TILE_SIZE) e->vy = 0.8f;
            if (e->y > 12 * TILE_SIZE) e->vy = -0.8f;
            if (player.vy >= 0 &&
                player.x + player.w > e->x && player.x < e->x + e->w &&
                player.y + player.h >= e->y - 2 && player.y + player.h <= e->y + 8) {
                player.y = e->y - player.h;
                player.vy = 0;
                player.on_ground = 1;
            }
            continue;
        }

        if (e->type == ENT_HPLATFORM) {
            e->x += e->vx;
            if (e->x < e->start_x - 24.0f) e->vx = 0.9f;
            if (e->x > e->start_x + 56.0f) e->vx = -0.9f;
            if (player.vy >= 0 &&
                player.x + player.w > e->x && player.x < e->x + e->w &&
                player.y + player.h >= e->y - 2 && player.y + player.h <= e->y + 8) {
                player.y = e->y - player.h;
                player.x += e->vx;
                player.vy = 0;
                player.on_ground = 1;
            }
            continue;
        }

        if (e->type == ENT_PLANT) {
            e->timer = (e->timer + 1) % 160;
            /* Don't emerge if Leo is right next to the pipe */
            if (e->timer < 10 && fabsf((player.x + player.w / 2) - (e->x + 7)) < 26.0f) {
                e->timer = 0;
            }
            if (e->timer < 40) {
                e->y = e->start_y + (40 - e->timer) * 0.5f;
            } else if (e->timer < 80) {
                e->y = e->start_y;
            } else if (e->timer < 120) {
                e->y = e->start_y + (e->timer - 80) * 0.5f;
            } else {
                e->y = e->start_y + 20.0f;
            }
            if (e->timer < 110 && rects_overlap(player.x, player.y, player.w, player.h, e->x, e->y, e->w, e->h)) {
                if (player.star_timer > 0) {
                    e->active = 0;
                    add_score(200, e->x, e->y);
                    play_sfx(SFX_KICK);
                } else {
                    hurt_player();
                }
            }
            continue;
        }

        if (e->type == ENT_BOSS) {
            float spd = (current_level == 11) ? 0.85f : ((current_level == 7) ? 0.70f : 0.55f);
            int jump_mod = (current_level == 11) ? 65 : ((current_level == 7) ? 80 : 95);
            int fire_mod = (current_level == 11) ? 75 : ((current_level == 7) ? 90 : 110);
            e->timer++;
            e->x += e->vx;
            if (e->x < 153 * TILE_SIZE) e->vx = spd;
            if (e->x > 163 * TILE_SIZE) e->vx = -spd;
            if (e->timer % jump_mod == 0 && e->on_ground) {
                e->vy = (current_level == 11) ? -4.4f : -3.8f;
                e->on_ground = 0;
            }
            if (e->timer % fire_mod == 40) {
                spawn_entity(ENT_BOSS_FIRE, e->x - 10, e->y + 8, -1);
                if (current_level >= 7) {
                    spawn_entity(ENT_BOSS_FIRE, e->x - 10, e->y + 20, -1);
                }
                if (current_level == 11) {
                    spawn_entity(ENT_BOSS_FIRE, e->x - 10, e->y - 4, -1);
                }
                play_sfx(SFX_FIREBALL);
            }
            e->vy += 0.18f;
            e->y += e->vy;
            if (e->y + e->h >= 10 * TILE_SIZE) {
                e->y = 10 * TILE_SIZE - e->h;
                e->vy = 0;
                e->on_ground = 1;
            }
            if (rects_overlap(player.x, player.y, player.w, player.h, e->x, e->y, e->w, e->h)) {
                hurt_player();
            }
            continue;
        }

        if (e->type == ENT_BOSS_FIRE) {
            e->x -= 1.8f;
            e->w = 16;
            e->h = 8;
            if (rects_overlap(player.x, player.y, player.w, player.h, e->x, e->y, e->w, e->h)) {
                hurt_player();
            }
            continue;
        }

        if (e->type == ENT_FLOWER) {
            if (rects_overlap(player.x, player.y, player.w, player.h, e->x, e->y, e->w, e->h)) {
                e->active = 0;
                if (player.power == STATE_SMALL) {
                    player.y -= 12;
                    player.h = 26;
                }
                player.power = STATE_FIRE;
                add_score(1000, e->x, e->y);
                play_sfx(SFX_POWERUP);
            }
            continue;
        }

        /* Gravity & tile physics for moving items & enemies */
        e->vy += (e->type == ENT_FLY_TURTLE) ? 0.14f : 0.24f;
        if (e->vy > 4.5f) e->vy = 4.5f;

        e->x += e->vx;
        int etx = (int)((e->vx > 0 ? (e->x + e->w) : e->x) / TILE_SIZE);
        int ety = (int)((e->y + e->h / 2) / TILE_SIZE);
        if (is_solid_tile(get_tile(etx, ety))) {
            e->vx = -e->vx;
            e->dir = -e->dir;
            e->x += e->vx * 2;
            if (e->type == ENT_SHELL && fabsf(e->vx) > 1.0f) play_sfx(SFX_BUMP);
        }

        e->y += e->vy;
        int etx_mid = (int)((e->x + e->w / 2) / TILE_SIZE);
        int ety_bot = (int)((e->y + e->h) / TILE_SIZE);
        if (is_solid_tile(get_tile(etx_mid, ety_bot))) {
            e->y = ety_bot * TILE_SIZE - e->h;
            if (e->type == ENT_STAR) {
                e->vy = -3.6f;
            } else if (e->type == ENT_FLY_TURTLE) {
                e->vy = -3.4f; /* Winged paratroopa hops continuously! */
            } else {
                e->vy = 0;
            }
            /* Turtles on sky bridges turn around at edges */
            if ((e->type == ENT_TURTLE || e->type == ENT_FLY_TURTLE) && ((current_level % 4) == 2)) {
                int ahead_tx = (int)((e->x + (e->vx > 0 ? e->w + 2 : -2)) / TILE_SIZE);
                if (!is_solid_tile(get_tile(ahead_tx, ety_bot))) {
                    e->vx = -e->vx;
                    e->dir = -e->dir;
                }
            }
        } else if (e->type == ENT_FLY_TURTLE && (current_level % 4) == 2) {
            /* In sky stages, winged paratroopas also hover-flap if falling below start_y + 24 */
            if (e->y > e->start_y + 20.0f && e->vy > 0) {
                e->vy = -2.8f;
            }
        }

        if (e->y > SCREEN_H + 32) {
            e->active = 0;
            continue;
        }

        /* Moving sliding shell bowls over other enemies! */
        if (e->type == ENT_SHELL && fabsf(e->vx) > 1.0f) {
            for (int j = 0; j < MAX_ENTITIES; j++) {
                if (i == j || !entities[j].active) continue;
                if (entities[j].type == ENT_WALKER || entities[j].type == ENT_TURTLE ||
                    entities[j].type == ENT_FLY_TURTLE || entities[j].type == ENT_SHELL) {
                    if (rects_overlap(e->x, e->y, e->w, e->h, entities[j].x, entities[j].y, entities[j].w, entities[j].h)) {
                        entities[j].active = 0;
                        add_score(200, entities[j].x, entities[j].y);
                        play_sfx(SFX_KICK);
                    }
                }
            }
        }

        /* Player <-> Entity Collision */
        if (rects_overlap(player.x, player.y, player.w, player.h, e->x, e->y, e->w, e->h)) {
            if (e->type == ENT_MUSHROOM) {
                e->active = 0;
                if (player.power == STATE_SMALL) {
                    player.power = STATE_SUPER;
                    player.y -= 12;
                    player.h = 26;
                }
                add_score(1000, e->x, e->y);
                play_sfx(SFX_POWERUP);
            } else if (e->type == ENT_ONEUP) {
                e->active = 0;
                lives++;
                add_score(1000, e->x, e->y);
                play_sfx(SFX_ONEUP);
            } else if (e->type == ENT_STAR) {
                e->active = 0;
                player.star_timer = 450;
                add_score(1000, e->x, e->y);
                play_sfx(SFX_POWERUP);
            } else if (e->type == ENT_WALKER || e->type == ENT_TURTLE ||
                       e->type == ENT_FLY_TURTLE || e->type == ENT_SHELL) {
                if (player.star_timer > 0) {
                    e->active = 0;
                    add_score(200, e->x, e->y);
                    play_sfx(SFX_KICK);
                } else if (e->type == ENT_SHELL && fabsf(e->vx) < 0.1f) {
                    /* Kick stationary shell! */
                    e->vx = (player.x + player.w / 2 < e->x + e->w / 2) ? 4.2f : -4.2f;
                    player.vy = -2.5f;
                    add_score(400, e->x, e->y);
                    play_sfx(SFX_KICK);
                } else if (player.vy > 0 && (player.y + player.h - player.vy) <= e->y + 12) {
                    /* Stomped from above! */
                    player.vy = -3.8f;
                    if (e->type == ENT_WALKER) {
                        e->active = 0;
                        add_score(100, e->x, e->y);
                        play_sfx(SFX_STOMP);
                    } else if (e->type == ENT_FLY_TURTLE) {
                        /* Clip wings -> becomes normal walking turtle! */
                        e->type = ENT_TURTLE;
                        e->vy = 0;
                        add_score(200, e->x, e->y);
                        play_sfx(SFX_STOMP);
                    } else if (e->type == ENT_TURTLE) {
                        e->type = ENT_SHELL;
                        e->h = 14;
                        e->y += 6;
                        e->vx = 0;
                        add_score(100, e->x, e->y);
                        play_sfx(SFX_STOMP);
                    } else if (e->type == ENT_SHELL) {
                        e->vx = 0;
                        add_score(100, e->x, e->y);
                        play_sfx(SFX_STOMP);
                    }
                } else {
                    hurt_player();
                }
            }
        }
    }
}

/* Render Player (Leo) */
static void draw_player(void) {
    if (player.invuln_timer > 0 && (player.invuln_timer / 3) % 2 == 1) return;

    int px = (int)(player.x - cam_x);
    int py = (int)player.y;

    Uint32 hat_col   = rgb(228, 32, 32);
    Uint32 shirt_col = rgb(228, 32, 32);
    Uint32 pants_col = rgb(32, 80, 220);
    Uint32 skin_col  = rgb(255, 204, 140);
    Uint32 shoe_col  = rgb(130, 60, 10);

    if (player.power == STATE_FIRE) {
        hat_col   = rgb(250, 250, 250);
        shirt_col = rgb(250, 250, 250);
        pants_col = rgb(228, 40, 20);
    }
    if (player.star_timer > 0) {
        int c = (frame_counter / 3) % 3;
        if (c == 0) { hat_col = rgb(255, 230, 0); pants_col = rgb(255, 80, 0); }
        else if (c == 1) { hat_col = rgb(50, 255, 80); pants_col = rgb(0, 180, 255); }
        else { hat_col = rgb(255, 80, 255); pants_col = rgb(255, 255, 100); }
    }

    int step = ((int)player.anim_frame) % 2;

    if (player.power == STATE_SMALL) {
        /* Cap */
        draw_rect(px + 1, py, 10, 3, hat_col);
        draw_rect(px + (player.dir > 0 ? 2 : 0), py + 1, 11, 2, hat_col);
        /* Face & Eyes */
        draw_rect(px + 2, py + 3, 8, 4, skin_col);
        draw_rect(px + (player.dir > 0 ? 7 : 3), py + 4, 2, 2, rgb(20, 20, 20));
        /* Shirt & Overalls */
        draw_rect(px + 1, py + 7, 10, 4, shirt_col);
        draw_rect(px + 3, py + 7, 6, 5, pants_col);
        /* Boots */
        draw_rect(px + (step ? 0 : 1), py + 12, 4, 2, shoe_col);
        draw_rect(px + (step ? 8 : 7), py + 12, 4, 2, shoe_col);
    } else if (player.crouch) {
        draw_rect(px + 1, py, 10, 3, hat_col);
        draw_rect(px + 2, py + 3, 8, 5, skin_col);
        draw_rect(px + 1, py + 8, 10, 6, pants_col);
        draw_rect(px + 1, py + 14, 10, 2, shoe_col);
    } else {
        /* Super / Fire Leo (26px tall) */
        draw_rect(px + 1, py, 10, 4, hat_col);
        draw_rect(px + (player.dir > 0 ? 2 : 0), py + 2, 12, 2, hat_col);
        draw_rect(px + 2, py + 4, 8, 6, skin_col);
        draw_rect(px + (player.dir > 0 ? 8 : 2), py + 5, 2, 2, rgb(20, 20, 20));
        /* Mustache / smile */
        draw_rect(px + (player.dir > 0 ? 6 : 2), py + 8, 4, 1, shoe_col);
        /* Torso & Overalls */
        draw_rect(px, py + 10, 12, 8, shirt_col);
        draw_rect(px + 2, py + 10, 8, 12, pants_col);
        /* Gold buttons on overalls */
        draw_rect(px + 3, py + 13, 2, 2, rgb(255, 220, 0));
        draw_rect(px + 7, py + 13, 2, 2, rgb(255, 220, 0));
        /* Hands */
        draw_rect(px, py + 14, 2, 3, skin_col);
        draw_rect(px + 10, py + 14, 2, 3, skin_col);
        /* Boots */
        draw_rect(px + (step ? 0 : 1), py + 22, 5, 4, shoe_col);
        draw_rect(px + (step ? 7 : 6), py + 22, 5, 4, shoe_col);
    }
}

/* Render Full Frame */
static void render_game(void) {
    int world_num = (current_level / 4) + 1;
    int stage_num = (current_level % 4) + 1;

    /* Sky / Background Color per Level */
    Uint32 bg_col;
    if (in_bonus_room || current_level == 1) bg_col = rgb(12, 16, 36);
    else if (current_level == 5) bg_col = rgb(14, 34, 64);      /* 2-2 Ice Cavern */
    else if (current_level == 9) bg_col = rgb(28, 12, 42);      /* 3-2 Haunted Cavern */
    else if (stage_num == 4) bg_col = rgb(20, 8, 12);           /* 1-4, 2-4, 3-4 Castles */
    else if (current_level == 4) bg_col = rgb(235, 165, 85);    /* 2-1 Golden Desert */
    else if (current_level == 6) bg_col = rgb(135, 195, 250);   /* 2-3 Snowy Cloud Peaks */
    else if (world_num == 3) bg_col = rgb(18, 24, 68);          /* 3-1, 3-3 Starlight Night */
    else bg_col = rgb(92, 148, 252);                            /* 1-1, 1-3 Classic Overworld */

    draw_rect(0, 0, SCREEN_W, SCREEN_H, bg_col);

    /* Twinkling Stars in World 3 Night Stages */
    if (!in_bonus_room && world_num == 3 && (stage_num == 1 || stage_num == 3)) {
        for (int s = 0; s < 28; s++) {
            int sx = (int)((s * 97 + 30 - (int)(cam_x * 0.2f)) % SCREEN_W);
            if (sx < 0) sx += SCREEN_W;
            int sy = 20 + (s * 37) % 140;
            Uint32 stcol = ((frame_counter / 12 + s) % 2) ? rgb(255, 255, 200) : rgb(160, 180, 255);
            draw_rect(sx, sy, 2, 2, stcol);
        }
    }

    /* Decorative Clouds, Hills & Pyramids on Overworld / Sky */
    if (!in_bonus_room && (stage_num == 1 || stage_num == 3)) {
        for (int i = 0; i < 18; i++) {
            int cx = (int)(i * 220 + 40 - cam_x * 0.4f);
            int cy = 36 + (i % 3) * 16;
            Uint32 ccol = (world_num == 3) ? rgb(170, 185, 230) : rgb(245, 250, 255);
            draw_rect(cx, cy, 32, 12, ccol);
            draw_rect(cx + 6, cy - 6, 20, 8, ccol);

            if (stage_num == 1) {
                int hx = (int)(i * 260 + 20 - cam_x * 0.7f);
                Uint32 hcol = (current_level == 4) ? rgb(210, 130, 40) :
                              ((world_num == 3) ? rgb(30, 110, 90) : rgb(24, 168, 40));
                draw_rect(hx, 13 * 16 - 18, 48, 18, hcol);
                draw_rect(hx + 8, 13 * 16 - 28, 32, 10, hcol);
            }
        }
    }

    /* Draw Map Tiles */
    int start_col = (int)(cam_x / TILE_SIZE) - 1;
    int end_col   = start_col + (SCREEN_W / TILE_SIZE) + 3;
    if (start_col < 0) start_col = 0;
    if (end_col >= MAP_W) end_col = MAP_W - 1;

    for (int r = 0; r < MAP_H; r++) {
        for (int c = start_col; c <= end_col; c++) {
            char t = map[r][c];
            if (t == ' ') continue;
            int sx = (int)(c * TILE_SIZE - cam_x);
            int sy = r * TILE_SIZE - bump_offset[r][c];

            if (t == '#') {
                Uint32 c1 = (stage_num == 2 || in_bonus_room) ? rgb(36, 100, 180) :
                            ((current_level == 4) ? rgb(195, 115, 25) : rgb(200, 76, 12));
                Uint32 c2 = (stage_num == 2 || in_bonus_room) ? rgb(90, 160, 240) :
                            ((current_level == 4) ? rgb(245, 190, 80) : rgb(252, 152, 56));
                draw_rect(sx, sy, 16, 16, c1);
                draw_rect(sx + 1, sy + 1, 6, 6, c2);
                draw_rect(sx + 9, sy + 1, 6, 6, c2);
                draw_rect(sx + 1, sy + 9, 6, 6, c2);
                draw_rect(sx + 9, sy + 9, 6, 6, c2);
            } else if (t == 'I') {
                /* Glossy Crystal Ice Block */
                draw_rect(sx, sy, 16, 16, rgb(40, 150, 220));
                draw_rect(sx + 1, sy + 1, 14, 14, rgb(135, 225, 255));
                draw_rect(sx + 2, sy + 2, 12, 3, rgb(235, 252, 255));
                draw_rect(sx + 3, sy + 8, 4, 4, rgb(210, 245, 255));
            } else if (t == 'J') {
                /* Red & White Springboard / Trampoline */
                draw_rect(sx + 1, sy, 14, 4, rgb(235, 40, 30));
                draw_rect(sx + 3, sy + 4, 10, 3, rgb(245, 245, 245));
                draw_rect(sx + 2, sy + 7, 12, 3, rgb(235, 40, 30));
                draw_rect(sx + 3, sy + 10, 10, 3, rgb(245, 245, 245));
                draw_rect(sx + 1, sy + 13, 14, 3, rgb(120, 120, 130));
            } else if (t == 'B') {
                Uint32 c1 = (stage_num == 2 || in_bonus_room) ? rgb(28, 88, 160) : rgb(180, 64, 10);
                Uint32 c2 = (stage_num == 2 || in_bonus_room) ? rgb(72, 148, 228) : rgb(228, 108, 36);
                draw_rect(sx, sy, 16, 16, c1);
                draw_rect(sx + 1, sy + 1, 6, 6, c2);
                draw_rect(sx + 9, sy + 1, 6, 6, c2);
                draw_rect(sx + 1, sy + 9, 14, 6, c2);
            } else if (t == '?' || t == 'M' || t == '*' || t == '1') {
                if (t == '1' && bump_offset[r][c] == 0) continue; /* Hidden 1-Up block until bumped! */
                int pulse = (frame_counter / 10) % 3;
                Uint32 qcol = (pulse == 0) ? rgb(255, 190, 20) : (pulse == 1 ? rgb(235, 150, 10) : rgb(255, 220, 60));
                draw_rect(sx, sy, 16, 16, rgb(140, 70, 0));
                draw_rect(sx + 1, sy + 1, 14, 14, qcol);
                draw_text(sx + 5, sy + 4, "?", rgb(120, 40, 0), 1);
            } else if (t == 'U') {
                draw_rect(sx, sy, 16, 16, rgb(110, 90, 75));
                draw_rect(sx + 2, sy + 2, 12, 12, rgb(150, 130, 110));
            } else if (t == 'S' || (t == 'F' && r >= 4)) {
                Uint32 c1 = (stage_num == 4) ? rgb(110, 110, 120) : rgb(170, 70, 15);
                Uint32 c2 = (stage_num == 4) ? rgb(165, 165, 178) : rgb(235, 135, 65);
                draw_rect(sx, sy, 16, 16, c1);
                draw_rect(sx + 2, sy + 2, 12, 12, c2);
            } else if (t == '=') {
                draw_rect(sx, sy, 16, 8, rgb(220, 150, 50));
                draw_rect(sx + 1, sy + 1, 14, 4, rgb(255, 210, 100));
            } else if (t == 'P' || t == 'E' || t == 'X') {
                draw_rect(sx, sy, 16, 16, rgb(0, 168, 0));
                draw_rect(sx + 2, sy + 1, 5, 14, rgb(110, 240, 70));
            } else if (t == 'p') {
                draw_rect(sx, sy, 16, 16, rgb(0, 168, 0));
                draw_rect(sx + 10, sy + 1, 4, 14, rgb(0, 110, 0));
            } else if (t == '[') {
                draw_rect(sx + 2, sy, 14, 16, rgb(0, 168, 0));
                draw_rect(sx + 4, sy, 4, 16, rgb(110, 240, 70));
            } else if (t == ']') {
                draw_rect(sx, sy, 14, 16, rgb(0, 168, 0));
                draw_rect(sx + 9, sy, 3, 16, rgb(0, 110, 0));
            } else if (t == 'o') {
                int w = ((frame_counter / 8) % 2) ? 8 : 4;
                draw_rect(sx + 8 - w / 2, sy + 2, w, 12, rgb(255, 215, 0));
                draw_rect(sx + 8 - w / 2 + 1, sy + 4, w > 2 ? w - 2 : 1, 8, rgb(255, 250, 160));
            } else if (t == 'C' || t == 'c') {
                draw_rect(sx + 7, sy - 16, 2, 32, rgb(220, 220, 220));
                Uint32 fcol = (t == 'c') ? rgb(40, 230, 60) : rgb(240, 50, 50);
                draw_rect(sx + 9, sy - 14, 10, 8, fcol);
            } else if (t == '|' || (t == 'F' && r <= 3)) {
                draw_rect(sx + 7, sy, 3, 16, rgb(180, 245, 180));
                if (r == 2) {
                    draw_rect(sx + 5, sy, 7, 7, rgb(255, 220, 0));
                    draw_rect(sx - 9, sy + 7, 16, 10, rgb(40, 220, 60));
                }
            } else if (t == 'K') {
                /* End-of-Level Brick Castle */
                draw_rect(sx - 16, sy - 48, 64, 64, rgb(185, 75, 20));
                draw_rect(sx + 6, sy - 22, 20, 38, rgb(20, 20, 20));
                for (int b = 0; b < 4; b++) {
                    draw_rect(sx - 16 + b * 18, sy - 56, 10, 8, rgb(185, 75, 20));
                }
            } else if (t == 'L') {
                draw_rect(sx, sy + 3, 16, 13, rgb(235, 45, 10));
                draw_rect(sx + ((frame_counter / 6 + c) % 4) * 2, sy + 3, 8, 3, rgb(255, 210, 0));
            } else if (t == 'A') {
                /* Golden Bridge Axe */
                draw_rect(sx + 6, sy + 4, 3, 12, rgb(180, 110, 40));
                draw_rect(sx + 2, sy + 2, 11, 7, rgb(255, 215, 0));
            }
        }
    }

    /* Draw Rotating Firebars (row >= 4) */
    {
        float rad = firebar_angle * 3.14159265f / 180.0f;
        for (int r = 4; r < MAP_H; r++) {
            for (int c = start_col; c <= end_col; c++) {
                if (map[r][c] == 'F') {
                    float cx = c * TILE_SIZE + 8 - cam_x;
                    float cy = r * TILE_SIZE + 8;
                    for (int k = 0; k <= 4; k++) {
                        int fx = (int)(cx + cosf(rad) * (k * 10) - 3);
                        int fy = (int)(cy + sinf(rad) * (k * 10) - 3);
                        draw_rect(fx, fy, 6, 6, (frame_counter % 4 < 2) ? rgb(255, 210, 20) : rgb(255, 70, 10));
                    }
                }
            }
        }
    }

    /* Draw Entities */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (!e->active) continue;
        int ex = (int)(e->x - cam_x);
        int ey = (int)e->y;
        if (ex < -48 || ex > SCREEN_W + 48) continue;

        if (e->type == ENT_WALKER) {
            draw_rect(ex + 1, ey, 12, 9, rgb(175, 75, 20));
            draw_rect(ex + 3, ey + 9, 8, 4, rgb(250, 210, 160));
            draw_rect(ex + 3, ey + 3, 2, 3, rgb(255, 255, 255));
            draw_rect(ex + 9, ey + 3, 2, 3, rgb(255, 255, 255));
            int step = (frame_counter / 8) % 2;
            draw_rect(ex + (step ? 1 : 2), ey + 12, 4, 2, rgb(20, 20, 20));
            draw_rect(ex + (step ? 9 : 8), ey + 12, 4, 2, rgb(20, 20, 20));
        } else if (e->type == ENT_TURTLE || e->type == ENT_FLY_TURTLE) {
            Uint32 shell_c = (e->type == ENT_FLY_TURTLE) ? rgb(225, 45, 45) : rgb(30, 185, 50);
            Uint32 hi_c    = (e->type == ENT_FLY_TURTLE) ? rgb(255, 150, 150) : rgb(120, 240, 110);
            draw_rect(ex + (e->dir > 0 ? 8 : 0), ey, 6, 7, rgb(255, 210, 40));
            draw_rect(ex + 2, ey + 6, 10, 11, shell_c);
            draw_rect(ex + 4, ey + 8, 6, 7, hi_c);
            if (e->type == ENT_FLY_TURTLE) {
                /* Flapping white feathered wings! */
                int flap = ((frame_counter / 6) % 2) ? -3 : 1;
                draw_rect(ex + 4, ey + 2 + flap, 7, 5, rgb(250, 250, 255));
                draw_rect(ex + 6, ey + flap, 4, 3, rgb(220, 235, 255));
            }
            draw_rect(ex + 2, ey + 17, 4, 3, rgb(255, 210, 40));
            draw_rect(ex + 8, ey + 17, 4, 3, rgb(255, 210, 40));
        } else if (e->type == ENT_SHELL) {
            draw_rect(ex + 1, ey + 2, 12, 11, rgb(30, 185, 50));
            draw_rect(ex + 3, ey + 4, 8, 7, rgb(120, 240, 110));
        } else if (e->type == ENT_PLANT) {
            draw_rect(ex + 1, ey, 12, 10, rgb(225, 35, 35));
            draw_rect(ex + 3, ey + 2, 3, 3, rgb(255, 255, 255));
            draw_rect(ex + 8, ey + 2, 3, 3, rgb(255, 255, 255));
            draw_rect(ex + 4, ey + 10, 6, 10, rgb(30, 180, 40));
        } else if (e->type == ENT_MUSHROOM) {
            draw_rect(ex + 1, ey, 12, 9, rgb(235, 40, 30));
            draw_rect(ex + 3, ey + 2, 3, 3, rgb(255, 255, 255));
            draw_rect(ex + 8, ey + 2, 3, 3, rgb(255, 255, 255));
            draw_rect(ex + 3, ey + 9, 8, 5, rgb(255, 215, 160));
        } else if (e->type == ENT_ONEUP) {
            draw_rect(ex + 1, ey, 12, 9, rgb(30, 200, 50));
            draw_rect(ex + 3, ey + 2, 3, 3, rgb(255, 255, 255));
            draw_rect(ex + 8, ey + 2, 3, 3, rgb(255, 255, 255));
            draw_rect(ex + 3, ey + 9, 8, 5, rgb(255, 215, 160));
        } else if (e->type == ENT_FLOWER) {
            draw_rect(ex + 2, ey + 1, 10, 7, (frame_counter % 6 < 3) ? rgb(255, 90, 20) : rgb(255, 230, 40));
            draw_rect(ex + 4, ey + 3, 6, 3, rgb(255, 255, 255));
            draw_rect(ex + 6, ey + 8, 2, 6, rgb(40, 200, 40));
        } else if (e->type == ENT_STAR) {
            Uint32 scol = (frame_counter % 6 < 3) ? rgb(255, 230, 20) : rgb(255, 140, 20);
            draw_rect(ex + 4, ey, 6, 14, scol);
            draw_rect(ex + 1, ey + 4, 12, 6, scol);
        } else if (e->type == ENT_PLATFORM || e->type == ENT_HPLATFORM) {
            Uint32 pcol = (e->type == ENT_HPLATFORM) ? rgb(255, 205, 50) : rgb(180, 70, 30);
            draw_rect(ex, ey, e->w, e->h, rgb(230, 230, 240));
            draw_rect(ex + 2, ey + 2, e->w - 4, e->h - 4, pcol);
        } else if (e->type == ENT_BOSS) {
            Uint32 bbody = (world_num == 3) ? rgb(190, 30, 40) : ((world_num == 2) ? rgb(40, 150, 210) : rgb(40, 165, 50));
            draw_rect(ex + 4, ey + 4, 22, 22, bbody);
            draw_rect(ex, ey + 2, 10, 10, rgb(220, 60, 30));
            draw_rect(ex + 2, ey, 4, 4, rgb(255, 240, 200));
            draw_rect(ex + 10, ey + 6, 14, 14, rgb(220, 150, 30));
            draw_rect(ex + 6, ey + 26, 6, 4, bbody);
            draw_rect(ex + 18, ey + 26, 6, 4, bbody);
        } else if (e->type == ENT_BOSS_FIRE) {
            draw_rect(ex, ey, 16, 6, (frame_counter % 4 < 2) ? rgb(255, 100, 10) : rgb(255, 220, 40));
        }
    }

    /* Draw Fireballs */
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        if (fireballs[i].active) {
            int fx = (int)(fireballs[i].x - cam_x);
            int fy = (int)fireballs[i].y;
            draw_rect(fx, fy, 6, 6, (frame_counter % 4 < 2) ? rgb(255, 80, 10) : rgb(255, 220, 30));
        }
    }

    /* Draw Player */
    draw_player();

    /* Draw Particles (Debris, Coins, Score Popups) */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        int px = (int)(particles[i].x - cam_x);
        int py = (int)particles[i].y;
        if (particles[i].type == 0) {
            draw_rect(px, py, 6, 6, particles[i].color);
        } else if (particles[i].type == 1) {
            draw_rect(px, py, 6, 10, rgb(255, 220, 0));
        } else if (particles[i].type == 2) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", particles[i].value);
            draw_text(px, py, buf, rgb(255, 255, 255), 1);
        }
    }

    /* Top Status HUD */
    char hud[128];
    snprintf(hud, sizeof(hud), "LEO %06d COINS %02d WORLD %d-%d LIVES %d TIME %03d",
             score, coins, world_num, stage_num, lives, level_timer);
    draw_rect(0, 0, SCREEN_W, 14, rgb(15, 15, 25));
    draw_text(10, 4, hud, rgb(255, 255, 255), 1);

    /* Title Screen Overlay */
    if (title_screen) {
        draw_rect(36, 34, 248, 154, rgb(20, 30, 70));
        draw_rect(38, 36, 244, 150, rgb(185, 65, 20));
        draw_text(58, 48, "SUPER LEO BROS.", rgb(255, 230, 60), 2);
        draw_text(52, 72, "3 WORLDS / 12 STAGES!", rgb(255, 255, 255), 1);

        char sel_buf[48];
        snprintf(sel_buf, sizeof(sel_buf), "START WORLD:  %d-%d",
                 (selected_start_level / 4) + 1, (selected_start_level % 4) + 1);
        draw_text(82, 98, sel_buf, rgb(120, 255, 120), 1);
        draw_text(52, 112, "(LEFT/RIGHT TO PICK 1-1..3-4)", rgb(220, 220, 220), 1);

        draw_text(50, 134, "ARROWS: MOVE    SPACE/X: JUMP", rgb(255, 255, 255), 1);
        draw_text(50, 146, "Z/SHIFT: RUN & FIREBALLS", rgb(255, 255, 255), 1);
        if ((frame_counter / 20) % 2 == 0) {
            draw_text(68, 166, "PRESS ENTER OR SPACE!", rgb(255, 235, 50), 1);
        }
    }

    /* Victory Screen Overlay */
    if (game_won) {
        draw_rect(40, 50, 240, 120, rgb(20, 80, 30));
        draw_rect(42, 52, 236, 116, rgb(30, 130, 50));
        draw_text(56, 68, "CONGRATULATIONS!", rgb(255, 235, 50), 2);
        draw_text(52, 98, "ALL 3 WORLDS CONQUERED!", rgb(255, 255, 255), 1);
        draw_text(54, 122, "PRESS ENTER TO PLAY AGAIN", rgb(200, 255, 200), 1);
        draw_text(68, 138, "PRESS ESC TO EXIT", rgb(220, 220, 220), 1);
    }

    SDL_Flip(screen);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void wasm_init(void) {
    memset(wasm_keys, 0, sizeof(wasm_keys));
    start_level(0, 0);
    render_game();
}

EMSCRIPTEN_KEEPALIVE Uint32 *wasm_get_fb(void) {
    return wasm_fb;
}

EMSCRIPTEN_KEEPALIVE void wasm_set_key(int k, int down) {
    if (k < 0 || k >= SDLK_LAST) return;
    int was_down = wasm_keys[k];
    wasm_keys[k] = down ? 1 : 0;

    if (down && !was_down) {
        if (k == SDLK_ESCAPE) {
            title_screen = 1;
        } else if (title_screen) {
            if (k == SDLK_LEFT && selected_start_level > 0) {
                selected_start_level--;
                play_sfx(SFX_COIN);
            } else if (k == SDLK_RIGHT && selected_start_level < max_unlocked_level) {
                selected_start_level++;
                play_sfx(SFX_COIN);
            } else if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_x) {
                title_screen = 0;
                checkpoint_reached = 0;
                start_level(selected_start_level, 0);
                play_sfx(SFX_POWERUP);
            }
        } else if (game_won) {
            if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_x) {
                game_won = 0;
                title_screen = 1;
                lives = 5;
                score = 0;
                coins = 0;
                checkpoint_reached = 0;
                start_level(0, 0);
            }
        }
    }
}

EMSCRIPTEN_KEEPALIVE void wasm_step_frame(void) {
    if (!title_screen) {
        update_game(wasm_keys);
    } else {
        frame_counter++;
    }
    render_game();
}
#else
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_ShowCursor(SDL_DISABLE);
    screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_AudioSpec desired;
    memset(&desired, 0, sizeof(desired));
    desired.freq = 22050;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = audio_callback;
    if (SDL_OpenAudio(&desired, NULL) >= 0) {
        SDL_PauseAudio(0);
    }

    start_level(0, 0);

    int running = 1;
    while (running) {
        Uint32 start_tick = SDL_GetTicks();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE || (k == SDLK_RETURN && (ev.key.keysym.mod & KMOD_RSHIFT))) {
                    running = 0;
                } else if (title_screen) {
                    if (k == SDLK_LEFT && selected_start_level > 0) {
                        selected_start_level--;
                        play_sfx(SFX_COIN);
                    } else if (k == SDLK_RIGHT && selected_start_level < max_unlocked_level) {
                        selected_start_level++;
                        play_sfx(SFX_COIN);
                    } else if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_x) {
                        title_screen = 0;
                        checkpoint_reached = 0;
                        start_level(selected_start_level, 0);
                        play_sfx(SFX_POWERUP);
                    }
                } else if (game_won) {
                    if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_x) {
                        game_won = 0;
                        title_screen = 1;
                        lives = 5;
                        score = 0;
                        coins = 0;
                        checkpoint_reached = 0;
                        start_level(0, 0);
                    }
                }
            }
        }

        const Uint8 *keys = SDL_GetKeyState(NULL);
        if (!title_screen) {
            update_game(keys);
        } else {
            frame_counter++;
        }

        render_game();

        Uint32 elapsed = SDL_GetTicks() - start_tick;
        if (elapsed < 16) {
            SDL_Delay(16 - elapsed);
        }
    }

    SDL_CloseAudio();
    SDL_Quit();
    return 0;
}
#endif
