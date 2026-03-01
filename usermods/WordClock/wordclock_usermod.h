#pragma once
#include "wled.h"

/*
 * WLED Word Clock Usermod
 * ========================
 * Displays the current time as words on an 11×10 LED matrix.
 *
 * Physical layout:
 *   LEDs 0–3   : Minute indicator dots (one lights per extra minute within 5-min block)
 *   LEDs 4–113 : 11×10 matrix in serpentine order (even rows L→R, odd rows R→L)
 *
 * Stencil layout (Row × Col, 0-indexed):
 *   Row 0: I  T  L  I  S  A  S  A  M  P  M
 *   Row 1: A  C  Q  U  A  R  T  E  R  D  C
 *   Row 2: T  W  E  N  T  Y  F  I  V  E  X
 *   Row 3: H  A  L  F  S  T  E  N  J  T  O
 *   Row 4: P  A  S  T  E  B  U  N  I  N  E
 *   Row 5: O  N  E  S  I  X  T  H  R  E  E
 *   Row 6: F  O  U  R  F  I  V  E  T  W  O
 *   Row 7: E  I  G  H  T  E  L  E  V  E  N
 *   Row 8: S  E  V  E  N  T  W  E  L  V  E
 *   Row 9: T  E  N  S  Z  O  C  L  O  C  K  (O = "O'" on stencil)
 *
 * Home Assistant integration: via WLED's built-in HTTP + MQTT APIs
 *   Usermod settings accessible at: GET/POST http://<wled-ip>/json/cfg  (key: "wc")
 *   Runtime state accessible at:    GET/POST http://<wled-ip>/json/state (key: "wc")
 */

// ──────────────────────────────────────────────────────────────────────────────
// Word segment descriptors
// ──────────────────────────────────────────────────────────────────────────────
struct WordSegment {
  uint8_t row;
  uint8_t col;
  uint8_t len;
};

// All addressable word positions
// clang-format off
static constexpr WordSegment WC_IT        = {0,  0, 2};
static constexpr WordSegment WC_IS        = {0,  3, 2};
static constexpr WordSegment WC_AM        = {0,  7, 2};
static constexpr WordSegment WC_PM        = {0,  9, 2};
static constexpr WordSegment WC_QUARTER   = {1,  2, 7};
static constexpr WordSegment WC_TWENTY    = {2,  0, 6};
static constexpr WordSegment WC_FIVE_MIN  = {2,  6, 4};   // FIVE (minutes)
static constexpr WordSegment WC_HALF      = {3,  0, 4};
static constexpr WordSegment WC_TEN_MIN   = {3,  5, 3};   // TEN  (minutes)
static constexpr WordSegment WC_TO        = {3,  9, 2};
static constexpr WordSegment WC_PAST      = {4,  0, 4};
static constexpr WordSegment WC_NINE      = {4,  7, 4};
static constexpr WordSegment WC_ONE       = {5,  0, 3};
static constexpr WordSegment WC_SIX       = {5,  3, 3};
static constexpr WordSegment WC_THREE     = {5,  6, 5};
static constexpr WordSegment WC_FOUR      = {6,  0, 4};
static constexpr WordSegment WC_FIVE_HR   = {6,  4, 4};   // FIVE (hours)
static constexpr WordSegment WC_TWO       = {6,  8, 3};
static constexpr WordSegment WC_EIGHT     = {7,  0, 5};
static constexpr WordSegment WC_ELEVEN    = {7,  5, 6};
static constexpr WordSegment WC_SEVEN     = {8,  0, 5};
static constexpr WordSegment WC_TWELVE    = {8,  5, 6};
static constexpr WordSegment WC_TEN_HR    = {9,  0, 3};   // TEN  (hours)
static constexpr WordSegment WC_OCLOCK    = {9,  5, 6};   // O'CLOCK (cols 5-10)
// clang-format on

// ──────────────────────────────────────────────────────────────────────────────
// Transition modes
// ──────────────────────────────────────────────────────────────────────────────
#define TRANS_RAINBOW_WAVE  0
#define TRANS_RADIAL_BLOOM  1
#define TRANS_CORNER_WIPE   2
#define TRANS_RANDOM        3

// ──────────────────────────────────────────────────────────────────────────────
// Main usermod class
// ──────────────────────────────────────────────────────────────────────────────
class WordClockUsermod : public Usermod {
public:
  static const char _name[];

private:
  // ── Matrix geometry ──────────────────────────────────────────────────────
  static const int COLS          = 11;
  static const int ROWS          = 10;
  static const int MINUTE_LEDS   = 4;
  static const int MATRIX_LEDS   = ROWS * COLS;          // 110
  static const int TOTAL_LEDS    = MINUTE_LEDS + MATRIX_LEDS; // 114

  // ── Configurable settings (saved to flash) ────────────────────────────────
  bool    enabled            = true;
  uint8_t wordBrightness     = 255;   // 0-255
  uint8_t bgBrightness       = 20;    // 0-255 background glow
  bool    showAmPm           = true;
  bool    randomWordColor    = false;
  bool    randomBgColor      = false;
  uint8_t transitionMode     = TRANS_RANDOM;  // 0-3
  uint16_t transitionMs      = 1200;          // transition duration ms
  // Word color
  uint8_t wordR = 255, wordG = 200, wordB = 100;
  // Background color
  uint8_t bgR   =   0, bgG   =   0, bgB   =  30;

  // ── Runtime state ─────────────────────────────────────────────────────────
  uint8_t  lastMinute        = 255;
  bool     transitionActive  = false;
  uint32_t transitionStart   = 0;
  uint8_t  activeTransMode   = 0;   // resolved (no RANDOM) during each transition
  bool     initDone          = false;

  // Buffered LED state for current time (set once per minute)
  uint32_t targetColors[TOTAL_LEDS]; // WRGB packed colors for target display

  // ── LED index helpers ─────────────────────────────────────────────────────

  /** Physical LED index for a matrix cell (row, col), accounting for serpentine wiring. */
  inline int matrixLed(int row, int col) const {
    int base = MINUTE_LEDS + row * COLS;
    return (row % 2 == 0) ? base + col : base + (COLS - 1 - col);
  }

  /** Pack R,G,B,W into WLED's RGBW uint32 format. */
  static inline uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
    return (uint32_t(w) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
  }

  /** Scale a packed color by brightness (0-255). */
  static inline uint32_t scaleBrightness(uint32_t c, uint8_t bri) {
    if (bri == 255) return c;
    uint8_t r = ((c >> 16) & 0xFF) * bri / 255;
    uint8_t g = ((c >>  8) & 0xFF) * bri / 255;
    uint8_t b = ((c      ) & 0xFF) * bri / 255;
    uint8_t w = ((c >> 24) & 0xFF) * bri / 255;
    return packColor(r, g, b, w);
  }

  // ── Color generators ──────────────────────────────────────────────────────

  /** Generate a random vibrant RGB color. */
  static uint32_t randomColor() {
    // Keep colors vivid: one channel full, one medium, one low-ish
    uint8_t r = random8();
    uint8_t g = random8();
    uint8_t b = random8();
    // Boost saturation
    uint8_t mx = max(r, max(g, b));
    if (mx > 0) {
      r = (uint8_t)((uint16_t)r * 255 / mx);
      g = (uint8_t)((uint16_t)g * 255 / mx);
      b = (uint8_t)((uint16_t)b * 255 / mx);
    }
    return packColor(r, g, b);
  }

  /** HSV→RGB packed color (hue 0-255). */
  static uint32_t hsvColor(uint8_t hue, uint8_t sat = 255, uint8_t val = 255) {
    CRGB c;
    c.setHSV(hue, sat, val);
    return packColor(c.r, c.g, c.b);
  }

  // ── Current resolved colors ───────────────────────────────────────────────
  uint32_t resolvedWordColor = 0;
  uint32_t resolvedBgColor   = 0;

  void resolveColors() {
    resolvedWordColor = randomWordColor ? randomColor()
                                       : packColor(wordR, wordG, wordB);
    resolvedBgColor   = randomBgColor  ? randomColor()
                                       : packColor(bgR, bgG, bgB);
  }

  // ── Word illumination helpers ─────────────────────────────────────────────

  void lightWord(const WordSegment& w, uint32_t color) {
    for (int i = 0; i < w.len; i++) {
      int idx = matrixLed(w.row, w.col + i);
      if (idx >= 0 && idx < TOTAL_LEDS)
        targetColors[idx] = scaleBrightness(color, wordBrightness);
    }
  }

  void clearTarget() {
    uint32_t bg = scaleBrightness(resolvedBgColor, bgBrightness);
    for (int i = 0; i < TOTAL_LEDS; i++) targetColors[i] = bg;
    // Minute dots default off (set later)
    for (int i = 0; i < MINUTE_LEDS; i++) targetColors[i] = 0;
  }

  // ── Time → word mapping ───────────────────────────────────────────────────

  /**
   * Populate targetColors[] for the given local hour (0-23) and minute (0-59).
   * The 4 minute-dot LEDs (indices 0-3) show (minute % 5) extra minutes.
   */
  void buildTimeDisplay(int hour, int minute) {
    clearTarget();

    int extraMins = minute % 5;
    int block     = minute / 5;   // 0-11

    // ── Minute indicator dots ────────────────────────────────────────────
    uint32_t dotColor = scaleBrightness(resolvedWordColor, wordBrightness);
    for (int i = 0; i < MINUTE_LEDS; i++) {
      targetColors[i] = (i < extraMins) ? dotColor : 0;
    }

    // ── "IT IS" always on ─────────────────────────────────────────────
    lightWord(WC_IT, resolvedWordColor);
    lightWord(WC_IS, resolvedWordColor);

    // ── Adjust hour for "TO" phrases ────────────────────────────────────
    int displayHour = hour % 12;
    if (block >= 7) displayHour = (displayHour + 1) % 12; // next hour for "TO"

    // ── Minute words ─────────────────────────────────────────────────────
    switch (block) {
      case  0:                                               // O'CLOCK
        lightWord(WC_OCLOCK, resolvedWordColor); break;
      case  1:                                               // FIVE PAST
        lightWord(WC_FIVE_MIN, resolvedWordColor);
        lightWord(WC_PAST, resolvedWordColor); break;
      case  2:                                               // TEN PAST
        lightWord(WC_TEN_MIN, resolvedWordColor);
        lightWord(WC_PAST, resolvedWordColor); break;
      case  3:                                               // QUARTER PAST
        lightWord(WC_QUARTER, resolvedWordColor);
        lightWord(WC_PAST, resolvedWordColor); break;
      case  4:                                               // TWENTY PAST
        lightWord(WC_TWENTY, resolvedWordColor);
        lightWord(WC_PAST, resolvedWordColor); break;
      case  5:                                               // TWENTY FIVE PAST
        lightWord(WC_TWENTY, resolvedWordColor);
        lightWord(WC_FIVE_MIN, resolvedWordColor);
        lightWord(WC_PAST, resolvedWordColor); break;
      case  6:                                               // HALF PAST
        lightWord(WC_HALF, resolvedWordColor);
        lightWord(WC_PAST, resolvedWordColor); break;
      case  7:                                               // TWENTY FIVE TO
        lightWord(WC_TWENTY, resolvedWordColor);
        lightWord(WC_FIVE_MIN, resolvedWordColor);
        lightWord(WC_TO, resolvedWordColor); break;
      case  8:                                               // TWENTY TO
        lightWord(WC_TWENTY, resolvedWordColor);
        lightWord(WC_TO, resolvedWordColor); break;
      case  9:                                               // QUARTER TO
        lightWord(WC_QUARTER, resolvedWordColor);
        lightWord(WC_TO, resolvedWordColor); break;
      case 10:                                               // TEN TO
        lightWord(WC_TEN_MIN, resolvedWordColor);
        lightWord(WC_TO, resolvedWordColor); break;
      case 11:                                               // FIVE TO
        lightWord(WC_FIVE_MIN, resolvedWordColor);
        lightWord(WC_TO, resolvedWordColor); break;
    }

    // ── Hour words ────────────────────────────────────────────────────────
    const WordSegment* hourWord = nullptr;
    switch (displayHour) {
      case  0: case 12: hourWord = &WC_TWELVE; break;
      case  1:          hourWord = &WC_ONE;    break;
      case  2:          hourWord = &WC_TWO;    break;
      case  3:          hourWord = &WC_THREE;  break;
      case  4:          hourWord = &WC_FOUR;   break;
      case  5:          hourWord = &WC_FIVE_HR;break;
      case  6:          hourWord = &WC_SIX;    break;
      case  7:          hourWord = &WC_SEVEN;  break;
      case  8:          hourWord = &WC_EIGHT;  break;
      case  9:          hourWord = &WC_NINE;   break;
      case 10:          hourWord = &WC_TEN_HR; break;
      case 11:          hourWord = &WC_ELEVEN; break;
    }
    if (hourWord) lightWord(*hourWord, resolvedWordColor);

    // ── AM / PM indicator ─────────────────────────────────────────────────
    if (showAmPm) {
      bool isPm = (hour >= 12);
      lightWord(isPm ? WC_PM : WC_AM, resolvedWordColor);
    }
  }

  // ── Transition rendering ──────────────────────────────────────────────────

  /**
   * Render transition frame at progress [0.0 .. 1.0].
   * At progress == 1.0 we snap to targetColors[].
   */
  void renderTransitionFrame(float progress) {
    switch (activeTransMode) {
      case TRANS_RAINBOW_WAVE:  renderRainbowWave(progress);  break;
      case TRANS_RADIAL_BLOOM:  renderRadialBloom(progress);  break;
      case TRANS_CORNER_WIPE:   renderCornerWipe(progress);   break;
      default:                  renderRainbowWave(progress);  break;
    }
  }

  /** Sweeping rainbow band that crosses the matrix then resolves to target. */
  void renderRainbowWave(float progress) {
    float waveFront = progress * (COLS + ROWS); // 0 → COLS+ROWS
    for (int row = 0; row < ROWS; row++) {
      for (int col = 0; col < COLS; col++) {
        int   idx    = matrixLed(row, col);
        float dist   = (float)(col + row); // diagonal distance
        float behind = waveFront - dist;
        uint32_t c;
        if (behind < 0.0f) {
          // Wave hasn't arrived → old background colour
          c = scaleBrightness(resolvedBgColor, bgBrightness);
        } else if (behind < 2.0f) {
          // In the wave band → rainbow
          uint8_t hue = (uint8_t)(dist * 255 / (COLS + ROWS));
          c = hsvColor(hue, 255, 255);
        } else {
          // Wave has passed → snap to target
          c = targetColors[idx];
        }
        setLed(idx, c);
      }
    }
    // Minute dots snap immediately
    for (int i = 0; i < MINUTE_LEDS; i++) setLed(i, targetColors[i]);
  }

  /** Radial bloom expanding from the matrix centre. */
  void renderRadialBloom(float progress) {
    float cx     = (COLS - 1) / 2.0f;
    float cy     = (ROWS - 1) / 2.0f;
    float maxR   = sqrtf(cx * cx + cy * cy);
    float radius = progress * (maxR + 1.0f);
    uint8_t hueOffset = (uint8_t)(progress * 128);

    for (int row = 0; row < ROWS; row++) {
      for (int col = 0; col < COLS; col++) {
        int   idx = matrixLed(row, col);
        float dx  = col - cx, dy = row - cy;
        float d   = sqrtf(dx * dx + dy * dy);
        uint32_t c;
        if (d > radius) {
          c = scaleBrightness(resolvedBgColor, bgBrightness);
        } else if (d > radius - 1.5f) {
          uint8_t hue = (uint8_t)(d / maxR * 255) + hueOffset;
          c = hsvColor(hue, 255, 255);
        } else {
          c = targetColors[idx];
        }
        setLed(idx, c);
      }
    }
    for (int i = 0; i < MINUTE_LEDS; i++) setLed(i, targetColors[i]);
  }

  /** Diagonal corner wipe from top-left to bottom-right. */
  void renderCornerWipe(float progress) {
    float totalDiag = (float)(COLS + ROWS - 2);
    float front     = progress * (totalDiag + 2.0f);
    for (int row = 0; row < ROWS; row++) {
      for (int col = 0; col < COLS; col++) {
        int   idx  = matrixLed(row, col);
        float diag = (float)(row + col);
        uint32_t c;
        if (diag > front) {
          c = scaleBrightness(resolvedBgColor, bgBrightness);
        } else if (diag > front - 1.5f) {
          uint8_t hue = (uint8_t)(diag / totalDiag * 255);
          c = hsvColor(hue, 255, 255);
        } else {
          c = targetColors[idx];
        }
        setLed(idx, c);
      }
    }
    for (int i = 0; i < MINUTE_LEDS; i++) setLed(i, targetColors[i]);
  }

  // ── Direct LED write helper ───────────────────────────────────────────────
  void setLed(int idx, uint32_t color) {
    if (idx < 0 || idx >= (int)strip.getLengthTotal()) return;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >>  8) & 0xFF;
    uint8_t b = (color      ) & 0xFF;
    uint8_t w = (color >> 24) & 0xFF;
    strip.setPixelColor(idx, r, g, b, w);
  }

  void applyTargetColors() {
    for (int i = 0; i < TOTAL_LEDS && i < (int)strip.getLengthTotal(); i++) {
      setLed(i, targetColors[i]);
    }
  }

  // ── Time acquisition ──────────────────────────────────────────────────────
  /** Return local time struct; returns false if time not synchronised. */
  bool getLocalTime(struct tm& t) {
    if (!WLED_CONNECTED) return false;
    updateLocalTime();          // WLED built-in: refreshes toki / localTime
    t = *localtime_r(&localTime, &t);
    return (localTime > 0);
  }

  // ── Trigger a new transition ──────────────────────────────────────────────
  void triggerTransition() {
    transitionActive = true;
    transitionStart  = millis();
    uint8_t mode     = transitionMode;
    if (mode == TRANS_RANDOM) mode = (uint8_t)(random8() % 3);
    activeTransMode  = mode;
  }

public:
  // ── Usermod lifecycle ─────────────────────────────────────────────────────
  void setup() override {
    initDone = true;
    // Initialise resolved colours
    resolveColors();
    // Pre-fill target with background
    clearTarget();
  }

  void loop() override {
    if (!enabled || strip.isUpdating()) return;

    struct tm t;
    if (!getLocalTime(t)) return;

    int currentMinute = t.tm_min;
    int currentHour   = t.tm_hour;

    // Detect minute rollover → start transition + rebuild display
    if (currentMinute != lastMinute) {
      lastMinute = currentMinute;
      resolveColors();               // re-randomise colours each minute if set
      buildTimeDisplay(currentHour, currentMinute);
      triggerTransition();
    }

    // First run: no transition, just show current time
    if (!initDone || lastMinute == 255) {
      lastMinute = currentMinute;
      resolveColors();
      buildTimeDisplay(currentHour, currentMinute);
      applyTargetColors();
      initDone = true;
      return;
    }

    // Animate transition or hold target
    if (transitionActive) {
      uint32_t elapsed = millis() - transitionStart;
      if (elapsed >= transitionMs) {
        transitionActive = false;
        applyTargetColors();
      } else {
        float progress = (float)elapsed / (float)transitionMs;
        renderTransitionFrame(progress);
      }
    } else {
      applyTargetColors();
    }

    strip.show();
  }

  // ── JSON state (read/write from HA or WLED app) ───────────────────────────
  void addToJsonState(JsonObject& root) override {
    JsonObject obj = root.createNestedObject(FPSTR(_name));
    obj[F("on")]           = enabled;
    obj[F("wordBri")]      = wordBrightness;
    obj[F("bgBri")]        = bgBrightness;
    obj[F("ampm")]         = showAmPm;
    obj[F("randWord")]     = randomWordColor;
    obj[F("randBg")]       = randomBgColor;
    obj[F("tranMode")]     = transitionMode;
    obj[F("tranMs")]       = transitionMs;
    // Colours as hex strings for easy HA color_picker integration
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", wordR, wordG, wordB);
    obj[F("wordColor")]    = buf;
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", bgR, bgG, bgB);
    obj[F("bgColor")]      = buf;
  }

  void readFromJsonState(JsonObject& root) override {
    JsonObject obj = root[FPSTR(_name)];
    if (obj.isNull()) return;
    getJsonValue(obj[F("on")],       enabled);
    getJsonValue(obj[F("wordBri")],  wordBrightness);
    getJsonValue(obj[F("bgBri")],    bgBrightness);
    getJsonValue(obj[F("ampm")],     showAmPm);
    getJsonValue(obj[F("randWord")], randomWordColor);
    getJsonValue(obj[F("randBg")],   randomBgColor);
    getJsonValue(obj[F("tranMode")], transitionMode);
    getJsonValue(obj[F("tranMs")],   transitionMs);

    // Parse hex color strings
    if (obj[F("wordColor")].is<const char*>()) {
      const char* hex = obj[F("wordColor")];
      if (hex[0] == '#' && strlen(hex) == 7) {
        wordR = (uint8_t)strtol(hex + 1, nullptr, 16) >> 16;
        wordG = ((uint8_t)strtol(hex + 1, nullptr, 16) >> 8) & 0xFF;
        wordB = ((uint8_t)strtol(hex + 1, nullptr, 16)) & 0xFF;
        // Re-parse properly
        uint32_t rgb = strtol(hex + 1, nullptr, 16);
        wordR = (rgb >> 16) & 0xFF;
        wordG = (rgb >>  8) & 0xFF;
        wordB =  rgb        & 0xFF;
      }
    }
    if (obj[F("bgColor")].is<const char*>()) {
      const char* hex = obj[F("bgColor")];
      if (hex[0] == '#' && strlen(hex) == 7) {
        uint32_t rgb = strtol(hex + 1, nullptr, 16);
        bgR = (rgb >> 16) & 0xFF;
        bgG = (rgb >>  8) & 0xFF;
        bgB =  rgb        & 0xFF;
      }
    }

    // Force redraw immediately
    lastMinute = 255;
  }

  // ── Persistent config ─────────────────────────────────────────────────────
  void addToConfig(JsonObject& root) override {
    JsonObject obj = root.createNestedObject(FPSTR(_name));
    obj[F("enabled")]      = enabled;
    obj[F("wordBri")]      = wordBrightness;
    obj[F("bgBri")]        = bgBrightness;
    obj[F("ampm")]         = showAmPm;
    obj[F("randWord")]     = randomWordColor;
    obj[F("randBg")]       = randomBgColor;
    obj[F("tranMode")]     = transitionMode;
    obj[F("tranMs")]       = transitionMs;
    obj[F("wordR")]        = wordR;
    obj[F("wordG")]        = wordG;
    obj[F("wordB")]        = wordB;
    obj[F("bgR")]          = bgR;
    obj[F("bgG")]          = bgG;
    obj[F("bgB")]          = bgB;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject obj = root[FPSTR(_name)];
    if (obj.isNull()) return false;
    bool changed = false;
    changed |= getJsonValue(obj[F("enabled")],   enabled);
    changed |= getJsonValue(obj[F("wordBri")],   wordBrightness);
    changed |= getJsonValue(obj[F("bgBri")],     bgBrightness);
    changed |= getJsonValue(obj[F("ampm")],      showAmPm);
    changed |= getJsonValue(obj[F("randWord")],  randomWordColor);
    changed |= getJsonValue(obj[F("randBg")],    randomBgColor);
    changed |= getJsonValue(obj[F("tranMode")],  transitionMode);
    changed |= getJsonValue(obj[F("tranMs")],    transitionMs);
    changed |= getJsonValue(obj[F("wordR")],     wordR);
    changed |= getJsonValue(obj[F("wordG")],     wordG);
    changed |= getJsonValue(obj[F("wordB")],     wordB);
    changed |= getJsonValue(obj[F("bgR")],       bgR);
    changed |= getJsonValue(obj[F("bgG")],       bgG);
    changed |= getJsonValue(obj[F("bgB")],       bgB);
    return changed;
  }

  // ── WLED UI config page ───────────────────────────────────────────────────
  void appendConfigData() override {
    // Generates HTML input elements in the WLED config UI
    oappend(SET_F("addInfo('wc:wordBri',1,'Word LED brightness (0-255)');"));
    oappend(SET_F("addInfo('wc:bgBri',1,'Background LED brightness (0-255)');"));
    oappend(SET_F("addInfo('wc:tranMode',1,'0=Rainbow Wave  1=Radial Bloom  2=Corner Wipe  3=Random');"));
    oappend(SET_F("addInfo('wc:tranMs',1,'Transition duration in milliseconds');"));
  }

  uint16_t getId() override { return USERMOD_ID_WORDCLOCK; }
};

const char WordClockUsermod::_name[] PROGMEM = "wc";
