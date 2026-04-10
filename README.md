# PigeonOS

Lightweight modular embedded OS for **ESP32 DEVKIT v1** (Arduino framework).

---

## Quick start

1. Install Arduino libraries (Library Manager):
   - **U8g2** — OLED driver
   - **SD** (built-in ESP32 core)

2. Add Lua 5.3 source files — see [Lua setup](#lua-setup).

3. Edit `Config.h` to match your wiring.

4. Copy `apps/` to the root of a FAT32-formatted SD card.

5. Compile and flash.

---

## Wiring

```
SH1106 OLED   →  ESP32
  VCC         →  3.3V
  GND         →  GND
  SDA         →  GPIO21
  SCL         →  GPIO22

SD module     →  ESP32
  VCC         →  3.3V
  GND         →  GND
  MOSI        →  GPIO23
  MISO        →  GPIO19
  SCK         →  GPIO18
  CS          →  GPIO5

Buttons (press to GND, other side to ESP32 pin):
  UP          →  GPIO34  ⚠ needs external 10kΩ pull-up to 3.3V
  DOWN        →  GPIO35  ⚠ needs external 10kΩ pull-up to 3.3V
  LEFT        →  GPIO32
  RIGHT       →  GPIO33
  SELECT      →  GPIO25
  BACK        →  GPIO26
  MENU        →  GPIO27
```

> **GPIO34/35 warning:** These pins are input-only on ESP32 and have no
> internal pull-up circuitry. You **must** add external 10 kΩ resistors from
> each pin to 3.3 V, or remap UP/DOWN to different GPIOs in `Config.h`.

---

## Lua setup

The Lua runtime is not an Arduino library — you include the C source directly.

1. Download **Lua 5.3** from https://www.lua.org/download.html
2. Extract and copy all `.c` / `.h` files into `PigeonOS/lua/`
3. **Delete** (or exclude from build) `lua.c` and `luac.c`
   (these contain `main()` for the standalone interpreter)
4. In `arduino-cli` or Arduino IDE, all `.c` files in the sketch folder are
   compiled automatically — no extra steps needed.

The `extern "C"` guards in `LuaEngine.h` handle C/C++ linkage.

---

## SD card layout

```
/
└── apps/
    ├── blink/
    │   └── app.lua
    └── your_app/
        └── app.lua
```

---

## File structure

```
PigeonOS/
├── main.ino            ← Kernel (setup + loop)
├── Config.h            ← All pin / timing / size constants
├── InputManager.h/.cpp ← Button polling, debounce, hold-repeat, event queue
├── UIEngine.h/.cpp     ← SH1106 wrapper (U8g2), menu renderer
├── SDManager.h/.cpp    ← SD init, app listing, file reading
├── LuaEngine.h/.cpp    ← Lua VM lifecycle + pigeon.* API bindings
├── AppLoader.h/.cpp    ← Launcher UI + app lifecycle (init/update/on_event)
├── lua/                ← Lua 5.3 source files (add manually — see above)
└── apps/
    └── blink/
        └── app.lua
```

---

## Writing Lua apps

Place each app in `/apps/<name>/app.lua` on the SD card.

### Lifecycle hooks

| Function | When called |
|---|---|
| `init()` | Once, immediately after the script loads |
| `update()` | Every kernel tick (~10 ms) while the app runs |
| `on_event(event)` | Once per input event |

All three are optional — implement only what you need.

### pigeon API

```lua
pigeon.getEvent()   -- returns event string or nil (polling style)
pigeon.time()       -- returns millis() as an integer
pigeon.print(str)   -- writes str to Serial (Serial.println)
```

### Event strings

| String | Button |
|---|---|
| `"up"` | UP |
| `"down"` | DOWN |
| `"left"` | LEFT |
| `"right"` | RIGHT |
| `"select"` | SELECT |
| `"back"` | BACK — **also exits the app before on_event is called** |
| `"menu"` | MENU |

### Minimal app

```lua
function init()
    pigeon.print("hello!")
end

function on_event(event)
    if event == "select" then
        pigeon.print("pressed at " .. pigeon.time() .. " ms")
    end
end

function update()
    -- runs every ~10 ms
end
```

---

## Input timing

| Parameter | Value |
|---|---|
| Poll interval | 10 ms (kernel tick) |
| Debounce | 20 ms stable |
| First repeat threshold | 300 ms held |
| Normal repeat interval | 150 ms |
| Fast repeat threshold | 800 ms held |
| Fast repeat interval | 70 ms |
| Event queue depth | 10 (oldest dropped on overflow) |

---

## Extending

- **Add pigeon.* bindings:** Register new `luaL_Reg` entries in
  `LuaEngine.cpp → s_pigeonLib[]`.
- **Expose UIEngine to Lua:** Add a `l_drawText` binding that calls
  `uiEngine.drawText()` via a pointer stored in the registry
  (`lua_pushlightuserdata` / `lua_rawsetp`).
- **New modules:** Follow the pattern — `.h` + `.cpp`, stack-allocated in
  `main.ino`, passed by reference to whatever needs them.
