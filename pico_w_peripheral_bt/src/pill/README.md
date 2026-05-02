# pill module — organization & migration plan

This directory contains the Smart Pill application's domain, HAL, and utility modules.

Current layout (existing):

- `src/pill/*.c`, `src/pill/*.h` — mixed implementation and public headers

Goals:

- Make public headers discoverable from a single include path (`include/pill/`).
- Separate hardware abstractions from domain logic (`hw/`, `alarm/`, `services/`).
- Keep changes incremental and build-safe (update `CMakeLists.txt` to add include + source paths).

Recommended structure (canonical):

```
pico_w_peripheral_bt/
  src/
    pill/
      include/pill/        # public headers (alarm_ctrl.h, alarm_model.h, ...)
      src/                 # implementation .c files compiled into the app
      hw/                  # hardware-specific implementations (pill_hw.c, battery_adc.c, leds.c, display.c)
      alarm/               # alarm domain (alarm_model, alarm_scheduler, alarm_ctrl)
      services/            # BLE validators, settings, etc. (ble_validation, app_settings)
      README.md            # this file
```

Mapping of current files to suggested targets:

- `alarm_model.{c,h}`, `alarm_scheduler.{c,h}`, `alarm_ctrl.{c,h}`  → `alarm/`
- `pill_hw.{c,h}`, `battery_adc.{c,h}`, `leds.{c,h}`, `display.{c,h}` → `hw/`
- `ble_validation.{c,h}`, `app_settings.{c,h}` → `services/`

Refactor steps (safe, incremental):

1. Create `include/pill/` and move public headers there. In `CMakeLists.txt` add:

   ```cmake
   target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/pill/include)
   ```

2. Move `.c` files into `src/pill/src/` and subfolders like `alarm/` and `hw/`.
3. Update `target_sources(app PRIVATE ...)` to list the new `.c` paths.
4. Run an incremental build (`build.ps1 -NoPristine`) and fix any broken includes.

Notes & tips:

- Preserve the existing include namespace `pill/...` so changes are minimally invasive. For example, keep `#include "pill/alarm_model.h"` but place headers under `include/pill/` and add that directory to the include path.
- Use `git mv` to preserve history when moving files.
- After the physical refactor, consider adding a single aggregator header `include/pill/pill.h` that re-exports the public API.

If you want, I can perform the physical refactor (move files, update includes, and update `CMakeLists.txt`) automatically — tell me to proceed and I'll do the moves and compile-checks incrementally.
