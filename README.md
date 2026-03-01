# WLED Word Clock

A WLED usermod that turns an 11×10 LED matrix into a word clock, complete with
minute-dot indicators, colourful minute-change transitions, and full Home
Assistant integration.

```
┌─────────────────────────────────────────┐
│  •  •  •  •   ← 4 minute-dot LEDs      │
│                                          │
│  I  T  L  I  S  A  S  A  M  P  M       │
│  A  C  Q  U  A  R  T  E  R  D  C       │
│  T  W  E  N  T  Y  F  I  V  E  X       │
│  H  A  L  F  S  T  E  N  J  T  O       │
│  P  A  S  T  E  B  U  N  I  N  E       │
│  O  N  E  S  I  X  T  H  R  E  E       │
│  F  O  U  R  F  I  V  E  T  W  O       │
│  E  I  G  H  T  E  L  E  V  E  N       │
│  S  E  V  E  N  T  W  E  L  V  E       │
│  T  E  N  S  Z  O' C  L  O  C  K       │
└─────────────────────────────────────────┘
```

---

## Hardware

| Part | Notes |
|------|-------|
| ESP32-C3 *or* ESP8266 D1 Mini | Main controller |
| WS2812B / SK6812 LED strip | 114 LEDs (4 + 110) |
| 5 V, ≥3 A power supply | ≈60 mA per LED at full white |
| 11×10 stencil / diffuser panel | See stencil image in repo |
| 470 Ω resistor on data line | Prevents ringing |
| 1000 µF capacitor across 5 V rail | Absorbs inrush current |

### Wiring

```
5V supply (+) ──────────────────────┐
                                     ├── LED strip 5V
5V supply (−) ──────┬───────────────┘
                    │                    ┌─ 470Ω ─ DATA_PIN (GPIO8 on C3 / GPIO2 on 8266)
                    └── ESP GND          └─────────── LED strip DIN

LED string order:
  [ dot₀ ][ dot₁ ][ dot₂ ][ dot₃ ]  ← minute indicators
  [ Row 0, L→R ]                      ← IT LI SA SA MPM
  [ Row 1, R→L ]                      ← serpentine
  ...
  [ Row 9, R→L ]
```

### Serpentine layout

Even rows (0, 2, 4, 6, 8) run **left → right**.  
Odd rows (1, 3, 5, 7, 9) run **right → left**.

---

## Building

### Prerequisites

```bash
pip install platformio
```

### Local build

```bash
# ESP32-C3
pio run -e esp32c3

# ESP8266
pio run -e esp8266

# Both
pio run
```

Output `.bin` files appear in `.pio/build/<env>/`.

### Flashing

```bash
# USB flash (replace /dev/ttyUSB0 with your port)
pio run -e esp32c3 -t upload --upload-port /dev/ttyUSB0

# Or with esptool directly
esptool.py --port /dev/ttyUSB0 write_flash 0x0 wordclock_esp32c3_*.bin
```

### OTA update

```bash
pio run -e esp32c3_ota   # uses mDNS hostname wordclock.local
```

---

## GitHub Actions (CI/CD)

The workflow at `.github/workflows/build.yml`:

| Trigger | Action |
|---------|--------|
| Push / PR to `main` | Build both targets, upload as workflow artifacts |
| Git tag `v*` | Build + create GitHub Release with `.bin` files attached |
| Manual dispatch | Build; optionally create a pre-release |

### Creating a release

```bash
git tag v1.0.0
git push origin v1.0.0
```

GitHub Actions will build and publish a release with `wordclock-esp32c3.bin`
and `wordclock-esp8266.bin` attached.

---

## WLED Setup

1. Flash the firmware.
2. On first boot connect to the **WLED-AP** hotspot and enter your WiFi credentials.
3. Open `http://wordclock.local` (or the IP shown in your router).
4. Go to **Config → Time & Macros**:
   - Enable **NTP**
   - Set your **UTC offset** (or use a timezone string)
5. Go to **Config → LED Preferences**:
   - Set LED count to **114**
   - Set type to **WS281X** (or SK6812 for RGBW)
6. Go to **Config → Usermods → wc** to configure the word clock.

### WLED usermod settings

| Key | Type | Description |
|-----|------|-------------|
| `on` | bool | Enable/disable word clock |
| `wordBri` | 0–255 | Word LED brightness |
| `bgBri` | 0–255 | Background glow brightness |
| `wordColor` | `#RRGGBB` | Word colour (ignored if randWord=true) |
| `bgColor` | `#RRGGBB` | Background colour (ignored if randBg=true) |
| `randWord` | bool | Randomise word colour each minute |
| `randBg` | bool | Randomise background colour each minute |
| `ampm` | bool | Light AM or PM indicator |
| `tranMode` | 0–3 | 0 Rainbow Wave, 1 Radial Bloom, 2 Corner Wipe, 3 Random |
| `tranMs` | ms | Transition animation duration |

**HTTP example** (set word colour to orange, random background, rainbow wave):

```bash
curl -X POST http://wordclock.local/json/state \
  -H 'Content-Type: application/json' \
  -d '{"wc":{"wordColor":"#FF8000","randBg":true,"tranMode":0}}'
```

---

## Home Assistant

### Auto-discovery

WLED is discovered automatically by HA's built-in **WLED integration** — just go
to **Settings → Devices & Services → Add Integration → WLED** and enter the IP.

This gives you the WLED light entity with on/off, brightness, and colour control.

### Full controls (package)

For word-clock–specific controls, import the package:

1. Add to `configuration.yaml`:
   ```yaml
   homeassistant:
     packages:
       wordclock: !include packages/wordclock_package.yaml
   ```
2. Copy `homeassistant/wordclock_package.yaml` to your `packages/` folder.
3. Replace `WORDCLOCK_IP` with your device's IP address.
4. Restart Home Assistant.

This creates:

| Entity | Type | Purpose |
|--------|------|---------|
| `input_number.wordclock_word_brightness` | Slider | Word LED brightness |
| `input_number.wordclock_bg_brightness` | Slider | Background brightness |
| `input_text.wordclock_word_color` | Text | Word colour hex (`#RRGGBB`) |
| `input_text.wordclock_bg_color` | Text | Background colour hex |
| `input_boolean.wordclock_random_word_color` | Toggle | Random word colour per minute |
| `input_boolean.wordclock_random_bg_color` | Toggle | Random background colour per minute |
| `input_boolean.wordclock_show_ampm` | Toggle | AM/PM indicator |
| `input_select.wordclock_transition_mode` | Dropdown | Transition animation |
| `input_number.wordclock_transition_ms` | Slider | Transition duration |

Automations in the package push every change immediately to the WLED device via
its REST API.

### Lovelace dashboard card

```yaml
type: vertical-stack
cards:
  - type: light
    entity: light.wordclock
    name: Word Clock
  - type: entities
    title: Word Clock Settings
    entities:
      - input_number.wordclock_word_brightness
      - input_number.wordclock_bg_brightness
      - input_text.wordclock_word_color
      - input_text.wordclock_bg_color
      - input_boolean.wordclock_random_word_color
      - input_boolean.wordclock_random_bg_color
      - input_boolean.wordclock_show_ampm
      - input_select.wordclock_transition_mode
      - input_number.wordclock_transition_ms
```

---

## Transition modes

| Mode | Description |
|------|-------------|
| 0 – Rainbow Wave | A diagonal rainbow band sweeps from top-left to bottom-right |
| 1 – Radial Bloom | Colours expand from the matrix centre outward |
| 2 – Corner Wipe | Diagonal colour sweep from the top-left corner |
| 3 – Random | Picks one of the above randomly each minute |

All transitions last `tranMs` milliseconds (default 1200 ms) then resolve to the
correct time display with the chosen word/background colours.

---

## File structure

```
wled-wordclock/
├── .github/
│   └── workflows/
│       └── build.yml          # CI/CD – builds .bin for ESP32-C3 & ESP8266
├── usermods/
│   └── WordClock/
│       └── wordclock_usermod.h  # Main WLED usermod (C++)
├── wled00/
│   └── usermods_list.cpp      # Registers usermod – copy into WLED source
├── homeassistant/
│   └── wordclock_package.yaml  # Full HA entity package
├── pio-scripts/
│   └── set_version.py         # Stamps .bin filename with git SHA + date
├── platformio.ini             # Build environments for both targets
└── README.md
```

---

## Integrating into the WLED source tree

1. Clone WLED:
   ```bash
   git clone https://github.com/Aircoookie/WLED.git
   cd WLED
   ```
2. Copy `usermods/WordClock/` into `WLED/usermods/`.
3. Replace (or merge) `wled00/usermods_list.cpp` with the provided file.
4. In `wled00/const.h` (or a new include), add:
   ```cpp
   #define USERMOD_ID_WORDCLOCK 99   // pick any unused ID
   ```
5. Copy `platformio.ini` or add the `[env:esp32c3]` / `[env:esp8266]` sections
   to WLED's existing `platformio.ini`.
6. Build as described above.

---

## Licence

MIT – see [WLED's licence](https://github.com/Aircoookie/WLED/blob/main/LICENSE)
for the upstream codebase.
