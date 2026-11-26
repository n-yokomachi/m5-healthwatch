// Device setup, text drawing in the Noto font, the eyes, the voice level bar, the mic
// task and the switch back to the launcher. Used by the HealthWatch screens and flow (hw/).
#include "shared.h"

#include <WiFi.h>
#include <esp_ota_ops.h>

#include "env_config.h"
#include "font_noto.h"

static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr size_t CHUNK = 800;                     // 50 ms per record() call
static constexpr size_t MAX_SAMPLES = SAMPLE_RATE * 20;  // 20 s limit

M5Canvas screen(&M5.Display);
int W, H, CX, CY;

int16_t* rec_buf;
volatile size_t rec_len = 0;   // samples queued to the mic (written by micTask)
volatile int level = 0;
volatile bool micOn = false;   // main -> micTask: keep recording
volatile bool micIdle = true;  // micTask -> main: all queued samples are filled

// ---------- text ----------
// Characters missing from the Noto subset are drawn with M5GFX's built-in font
// (JIS level 1 and 2). That font lives in its own small sprite so the Noto font on
// the screen never has to be reloaded; each character is drawn there and pasted over.
static M5Canvas fallbackCv(&screen);

// The VLW data starts with a glyph table sorted by code point (24-byte header,
// then 28 bytes per glyph whose first field is the big-endian code point).
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static bool inNoto(const String& ch) {
  if (ch.length() == 1) return true;  // ASCII is always in the subset
  const uint8_t* u = (const uint8_t*)ch.c_str();
  uint32_t cp = ch.length() == 2 ? (u[0] & 0x1F) << 6 | (u[1] & 0x3F)
              : ch.length() == 3 ? (u[0] & 0x0F) << 12 | (u[1] & 0x3F) << 6 | (u[2] & 0x3F)
              : 0x10000;  // 4-byte characters are outside the 16-bit table
  int lo = 0, hi = (int)be32(FONT_NOTO) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t v = be32(FONT_NOTO + 24 + 28 * mid);
    if (v == cp) return true;
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return false;
}

static int charWidth(M5Canvas& dst, const String& ch) {
  return inNoto(ch) ? dst.textWidth(ch) : fallbackCv.textWidth(ch);
}

int lineWidth(M5Canvas& dst, const String& line) {
  int w = 0;
  for (size_t i = 0; i < line.length();) {
    size_t j = i + 1;
    while (j < line.length() && (line[j] & 0xC0) == 0x80) ++j;
    w += charWidth(dst, line.substring(i, j));
    i = j;
  }
  return w;
}

void drawLine(M5Canvas& dst, const String& line, int x, int y, uint16_t color) {
  String run;  // consecutive characters the Noto subset has
  auto flush = [&]() { if (run.length()) { dst.drawString(run, x, y); x += dst.textWidth(run); run = ""; } };
  for (size_t i = 0; i < line.length();) {
    size_t j = i + 1;
    while (j < line.length() && (line[j] & 0xC0) == 0x80) ++j;
    String ch = line.substring(i, j);
    i = j;
    if (inNoto(ch)) { run += ch; continue; }
    flush();
    fallbackCv.fillScreen(TFT_BLACK);
    fallbackCv.setTextColor(color);
    fallbackCv.drawString(ch, 0, 0);
    fallbackCv.pushSprite(&dst, x, y + 2, TFT_BLACK);
    x += fallbackCv.textWidth(ch);
  }
  flush();
}

// Wrapping is done here (not by the canvas) so each line can be measured and aligned.
void wrapLines(const String& s, int maxW, std::vector<String>& out) {
  String line;
  size_t i = 0;
  while (i < s.length()) {
    size_t j = i + 1;
    while (j < s.length() && (s[j] & 0xC0) == 0x80) ++j;  // one UTF-8 character
    String ch = s.substring(i, j);
    i = j;
    if (ch == "\n") { out.push_back(line); line = ""; continue; }
    if (line.length() && lineWidth(screen, line + ch) > maxW) { out.push_back(line); line = ""; }
    line += ch;
  }
  if (line.length() || out.empty()) out.push_back(line);
}

// ---------- eyes ----------
// Two white rounded eyes. They blink and glance around on their own. Gaze is in -1..1 on both axes.
struct Eyes {
  float x = 0, y = 0;    // current gaze
  float tx = 0, ty = 0;  // target gaze
  uint32_t nextGlanceMs = 2000, nextBlinkMs = 3000, blinkStartMs = 0;
};
static Eyes eyes;
static constexpr int EYE_W = 78, EYE_H = 118, EYE_GAP = 150, EYE_R = 37;  // corner radius (39 = fully round ends)
static constexpr int EYE_LOOK_X = 110, EYE_LOOK_Y = 100;  // px at full gaze

void lookAt(float x, float y) { eyes.tx = constrain(x, -1.0f, 1.0f); eyes.ty = constrain(y, -1.0f, 1.0f); }

void updateEyes(uint32_t now) {
  if (now > eyes.nextGlanceMs) {
    // idle glances: mostly near the centre, sometimes further out
    bool far = random(4) == 0;
    float gx = random(-100, 101) / (far ? 100.0f : 300.0f), gy = random(-100, 101) / (far ? 150.0f : 400.0f);
    lookAt(gx, gy);
    eyes.nextGlanceMs = now + 1200 + random(2800);
  }
  if (now > eyes.nextBlinkMs && !eyes.blinkStartMs) eyes.blinkStartMs = now;
  if (eyes.blinkStartMs && now - eyes.blinkStartMs > 170) {
    eyes.blinkStartMs = 0;
    eyes.nextBlinkMs = now + 2200 + random(3500) - (random(5) == 0 ? 2000 : 0);  // sometimes a quick double blink
  }
  eyes.x += (eyes.tx - eyes.x) * 0.18f;
  eyes.y += (eyes.ty - eyes.y) * 0.18f;
}

void drawEyes(int cx, int cy, float scale) {
  uint32_t now = millis();
  float open = 1.0f;
  if (eyes.blinkStartMs) {
    float u = (now - eyes.blinkStartMs) / 170.0f;
    open = fabsf(u * 2 - 1);  // 1 -> 0 -> 1
  }
  float ox = eyes.x * EYE_LOOK_X * scale, oy = eyes.y * EYE_LOOK_Y * scale;
  for (int side = -1; side <= 1; side += 2) {
    // turning towards one side moves that eye to the far side of the head: it gets
    // a little smaller and the near eye a little bigger (perspective)
    float persp = 1 - side * eyes.x * 0.15f;
    float tilt = 1 - fabsf(eyes.y) * 0.15f;  // looking up or down: seen at an angle, a little flatter
    float ew = EYE_W * scale * persp;
    float eh = fmaxf(EYE_H * scale * persp * tilt * open, 8 * scale);
    float ex = cx + side * EYE_GAP * scale / 2 + ox, ey = cy + oy;
    screen.fillSmoothRoundRect(ex - ew / 2, ey - eh / 2, ew, eh, fminf(EYE_R * scale, eh / 2), TFT_WHITE);
  }
}

// thin voice level bar while listening
void drawLevel(int cy) {
  int w = 160, h = 6, x = CX - w / 2;
  screen.fillRoundRect(x, cy, w, h, 3, 0x2104);
  screen.fillRoundRect(x, cy, w * level / 40, h, 3, 0xFE60);
}

// ---------- mic ----------
// Keeps the mic DMA queue fed regardless of how long drawing takes.
static void micTask(void*) {
  for (;;) {
    if (!micOn) {
      if (!micIdle && !M5.Mic.isRecording()) micIdle = true;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    micIdle = false;
    size_t n = rec_len;
    if (n + CHUNK > MAX_SAMPLES) { micOn = false; continue; }
    if (M5.Mic.record(&rec_buf[n], CHUNK, SAMPLE_RATE)) {
      rec_len = n + CHUNK;
      if (n >= CHUNK) {  // level of the previous (finished) chunk
        int32_t peak = 0;
        for (size_t i = n - CHUNK; i < n; ++i) peak = max<int32_t>(peak, abs(rec_buf[i]));
        level = min(40, (int)(peak / 400));
      }
    }
  }
}

// ---------- device ----------
void deviceBegin() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setBrightness(150);
  W = M5.Display.width(); H = M5.Display.height();
  CX = W / 2; CY = H / 2;

  screen.setPsram(true); screen.setColorDepth(16); screen.createSprite(W, H);
  // fonts are loaded once per canvas; drawing code never switches fonts
  screen.loadFont(FONT_NOTO);
  fallbackCv.setColorDepth(16);
  fallbackCv.createSprite(28, 28);
  fallbackCv.setFont(&fonts::lgfxJapanGothic_24);
  fallbackCv.setTextDatum(textdatum_t::top_left);

  rec_buf = (int16_t*)heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  M5.Speaker.end();
  auto mc = M5.Mic.config();
  mc.sample_rate = SAMPLE_RATE;
  mc.noise_filter_level = 64;
  M5.Mic.config(mc);
  // the mic is started only while the yellow key is held to save power
  xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 5, nullptr, 1);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);  // modem sleep while idle
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void switchToMenu() {
  const esp_partition_t* menu =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  esp_app_desc_t desc;
  if (!menu || esp_ota_get_partition_description(menu, &desc) != ESP_OK) {
    printf("launcher not found in ota_0\n");
    return;
  }
  M5.Display.fillScreen(TFT_BLACK);
  esp_ota_set_boot_partition(menu);
  M5.delay(300);
  esp_restart();
}
