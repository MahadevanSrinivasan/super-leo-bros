# Super Leo Bros.

An original, copyright-free classic 2D platformer written in a **single C99 source file** (`super_leo_bros.c`) with zero external asset files. Runs natively on **Raspberry Pi (RetroPie / SDL 1.2)** and in the browser via **WebAssembly (Emscripten)**.

**Play Live in Your Browser:** [https://theshybulb.com/super-leo-bros/](https://theshybulb.com/super-leo-bros/)

---

## Features
- **Classic Momentum & Jump Physics**: Acceleration, skidding turnarounds, sprint-jump height bonus, and variable jump height depending on how long you hold Jump.
- **3-Tier Power-Up Progression**: Small Leo &rarr; Super Leo (Mushroom, smashes brick blocks) &rarr; Fire Leo (Fire Flower, throws bouncing fireballs) + temporary Rainbow Starman invincibility.
- **Classic Enemy Trio + Castle Boss**:
  - 1-stomp mushroom walkers
  - 2-state turtles that retract into kickable shells and bowl over other enemies
  - Pop-up pipe plants that wait if Leo stands next to the pipe
  - World 1-4 Castle Boss on a bridge over lava with a Golden Axe
- **4-Stage World Arc**:
  - **World 1-1**: Sunny Overworld (with an enterable secret underground coin pipe at the 4th green pipe!)
  - **World 1-2**: Underground Cavern
  - **World 1-3**: Sky Athletic Treetop Bridges
  - **World 1-4**: Lava Castle & Bridge Boss Fight
- **Kid-Friendly Progression**: Mid-level Checkpoint Flags, 5 starting lives, 100 coins = 1-Up, unlimited continues on the current level, and Stage Select (`1-1` to `1-4`) on the Title Screen.

---

## Controls

| Action | Keyboard |
| :--- | :--- |
| **Move Left / Right** | `Left` / `Right` Arrow Keys (or `A` / `D`) |
| **Jump** (hold for higher jump) | `Space`, `X`, `Up`, or `W` |
| **Run Fast & Throw Fireballs** | Hold `Z`, `Shift`, or `Ctrl` |
| **Crouch / Enter Secret Pipe** | `Down` or `S` |
| **Select Starting World (`1-1`..`1-4`)** | `Left` / `Right` on Title Screen, then `Enter` or `Space` |
| **Return to Title / Exit** | `Esc` |

---

## Building

### 1. Native Linux / Raspberry Pi (SDL 1.2)
```bash
gcc -O2 -Wall -Wextra super_leo_bros.c -o super_leo_bros $(sdl-config --cflags --libs) -lm
./super_leo_bros
```

### 2. WebAssembly (Emscripten)
```bash
emcc -O2 super_leo_bros.c -o super_leo_bros.js \
  -s EXPORTED_FUNCTIONS='["_wasm_init","_wasm_step_frame","_wasm_get_fb","_wasm_set_key"]' \
  -s EXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  -lm
```

## License
MIT License
