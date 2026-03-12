/*
 * Guitar Hero Style Game for Binghe 4" OLED (ST7796S)
 * Arduino IDE
 *
 * ─── HOW TO EDIT THE BEATMAP ──────────────────────────────────────────
 * Edit the BEATMAP array below. Each entry is { timeMs, lane }
 *   timeMs : when (in milliseconds from game start) the note should
 *            reach the HIT ZONE at the bottom
 *   lane   : 0 = Red, 1 = Green, 2 = Yellow, 3 = Blue
 *
 * Notes MUST be sorted by timeMs (earliest first).
 * ──────────────────────────────────────────────────────────────────────
 *
 * WIRING (adjust pins to match your board):
 *   ST7796S CS   -> 10
 *   ST7796S DC   -> 9
 *   ST7796S RST  -> 8
 *   ST7796S MOSI -> 11 (MOSI)
 *   ST7796S SCK  -> 13 (SCK)
 *   ST7796S BL   -> 3.3V 
 *
 * Buttons (active LOW with INPUT_PULLUP):
 *   Lane 0 (Red)    -> PIN_BTN_0
 *   Lane 1 (Green)  -> PIN_BTN_1
 *   Lane 2 (Yellow) -> PIN_BTN_2
 *   Lane 3 (Blue)   -> PIN_BTN_3
 *
 * Libraries needed (install via Library Manager):
 *   Adafruit GFX Library
 *   Adafruit ST7735 and ST7789 Library  (supports ST7796S via ST7789)
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>   // ST7796S is compatible via this driver
#include <SPI.h>


#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   4

#define PIN_BTN_0  12   
#define PIN_BTN_1  13   
#define PIN_BTN_2  14   
#define PIN_BTN_3  27   

#define SCREEN_W  320
#define SCREEN_H  480

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);


#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_YELLOW  0xFFE0
#define C_BLUE    0x001F
#define C_GREY    0x2945   
#define C_LGREY   0x7BEF   

const uint16_t LANE_COLORS[4] = { C_RED, C_GREEN, C_YELLOW, C_BLUE };


#define NUM_LANES      4
#define LANE_W         70       
#define LANE_GAP        5      
#define NOTE_RADIUS    22       
#define HIT_ZONE_Y    430       
#define HIT_ZONE_R     26       
#define HIT_WINDOW     40       
#define NOTE_SPEED     220     

// Pre-compute lane centre X positions
// Total lanes width = 4*70 + 3*5 = 295. Centre on 320px screen => left margin 12
#define LANE_MARGIN    13
int laneCX[NUM_LANES];          // filled in setup()

// ── ═══════════════════════════════════════════════════════════ ────────
//    BEATMAP  –  EDIT THIS SECTION TO CHOREOGRAPH YOUR SONG
//    Format:  { arrivalTimeMs,  lane }
//    arrivalTimeMs = time (ms) when the note should be AT the hit zone
//    lane = 0 (Red) | 1 (Green) | 2 (Yellow) | 3 (Blue)
// ─────────────────────────────────────────────────────────────────────
struct BeatmapEntry {
  uint32_t arrivalMs;  // when note reaches hit zone (ms from game start)
  uint8_t  lane;
};

const BeatmapEntry BEATMAP[] = {
 
  {  1000, 0 },
  {  1500, 1 },
  {  2000, 2 },
  {  2500, 3 },

  {  3000, 0 },
  {  3000, 2 },   
  {  3500, 1 },
  {  4000, 3 },
  {  4500, 0 },

  {  5000, 1 },
  {  5250, 2 },
  {  5500, 1 },
  {  5750, 0 },
  {  6000, 3 },

  {  6500, 0 },
  {  6750, 1 },
  {  7000, 2 },
  {  7250, 3 },
  {  7500, 2 },
  {  7750, 1 },
  {  8000, 0 },
 
};

const int BEATMAP_LEN = sizeof(BEATMAP) / sizeof(BEATMAP[0]);

#define MAX_NOTES  20

struct Note {
  bool     active;
  uint8_t  lane;
  float    y;        
  int16_t  prevY;   
};

Note notes[MAX_NOTES];


int      score        = 0;
int      combo        = 0;
int      beatmapIndex = 0;   // next BEATMAP entry to spawn
uint32_t gameStartMs  = 0;

bool     btnState[NUM_LANES]     = { false };
bool     btnPrevState[NUM_LANES] = { false };
bool     btnJustPressed[NUM_LANES] = { false };


int      prevScore = -1;
int      prevCombo = -1;


void clearNote(int i) {
  if (notes[i].prevY >= 0) {
    // Erase old note by redrawing lane background colour over it
    tft.fillCircle(laneCX[notes[i].lane], notes[i].prevY + NOTE_RADIUS,
                   NOTE_RADIUS + 1, C_GREY);
  }
}

void drawNote(int i) {
  int cx = laneCX[notes[i].lane];
  int cy = (int)notes[i].y + NOTE_RADIUS;
  tft.fillCircle(cx, cy, NOTE_RADIUS, LANE_COLORS[notes[i].lane]);
  // white ring
  tft.drawCircle(cx, cy, NOTE_RADIUS, C_WHITE);
  notes[i].prevY = (int16_t)notes[i].y;
}

void drawHitZones() {
  for (int i = 0; i < NUM_LANES; i++) {
    tft.drawCircle(laneCX[i], HIT_ZONE_Y, HIT_ZONE_R, LANE_COLORS[i]);
    tft.drawCircle(laneCX[i], HIT_ZONE_Y, HIT_ZONE_R - 1, LANE_COLORS[i]);
  }
}

void flashHitZone(int lane) {
  tft.fillCircle(laneCX[lane], HIT_ZONE_Y, HIT_ZONE_R, C_WHITE);
  delay(30);
  tft.fillCircle(laneCX[lane], HIT_ZONE_Y, HIT_ZONE_R, C_BLACK);
  drawHitZones();
}

void updateScoreDisplay() {
  if (score != prevScore) {
    tft.fillRect(0, 0, SCREEN_W, 28, C_BLACK);
    tft.setCursor(4, 6);
    tft.setTextColor(C_WHITE);
    tft.setTextSize(2);
    tft.print("Score: ");
    tft.print(score);
    prevScore = score;
  }
  if (combo != prevCombo) {
    tft.fillRect(0, 30, SCREEN_W, 22, C_BLACK);
    if (combo > 1) {
      tft.setCursor(4, 32);
      tft.setTextColor(C_YELLOW);
      tft.setTextSize(2);
      tft.print("Combo x");
      tft.print(combo);
    }
    prevCombo = combo;
  }
}

// ── Setup ─────────────────────────────────────────────────────────────
void setup() {
  for (int i = 0; i < NUM_LANES; i++) {
    pinMode(PIN_BTN_0 + i, INPUT_PULLUP);
  }

  for (int i = 0; i < NUM_LANES; i++) {
    laneCX[i] = LANE_MARGIN + i * (LANE_W + LANE_GAP) + LANE_W / 2;
  }

  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(0);       // portrait; change to 1/2/3 if needed
  tft.fillScreen(C_BLACK);

  for (int i = 0; i < NUM_LANES; i++) {
    tft.fillRect(LANE_MARGIN + i * (LANE_W + LANE_GAP), 55,
                 LANE_W, SCREEN_H - 55 - 10, C_GREY);
  }

  drawHitZones();

  for (int i = 0; i < MAX_NOTES; i++) {
    notes[i].active = false;
    notes[i].prevY  = -1;
  }

  updateScoreDisplay();


  tft.setCursor(80, 220);
  tft.setTextColor(C_WHITE); tft.setTextSize(4);
  tft.print("READY?");
  delay(2000);
  tft.fillRect(0, 200, SCREEN_W, 80, C_BLACK);

  gameStartMs = millis();
}


void loop() {
  static uint32_t lastMs = 0;
  uint32_t nowMs  = millis();
  uint32_t gameMs = nowMs - gameStartMs;        // elapsed game time
  float    dt     = (nowMs - lastMs) / 1000.0f; // seconds since last frame
  lastMs = nowMs;


  for (int i = 0; i < NUM_LANES; i++) {
    btnState[i]      = (digitalRead(PIN_BTN_0 + i) == LOW);
    btnJustPressed[i] = btnState[i] && !btnPrevState[i];
    btnPrevState[i]  = btnState[i];
  }


  uint32_t travelMs = (uint32_t)(((float)(HIT_ZONE_Y + NOTE_RADIUS * 2) / NOTE_SPEED) * 1000.0f);

  while (beatmapIndex < BEATMAP_LEN) {
    uint32_t arrivalMs = BEATMAP[beatmapIndex].arrivalMs;
    // Spawn the note `travelMs` before it should arrive
    if (gameMs >= arrivalMs - travelMs) {
      // Find a free slot
      for (int i = 0; i < MAX_NOTES; i++) {
        if (!notes[i].active) {
          notes[i].active = true;
          notes[i].lane   = BEATMAP[beatmapIndex].lane;
          notes[i].y      = -(float)(NOTE_RADIUS * 2);
          notes[i].prevY  = -1;
          break;
        }
      }
      beatmapIndex++;
    } else {
      break; // beatmap is sorted; no need to look further
    }
  }


  for (int i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active) continue;
    clearNote(i);
    notes[i].y += NOTE_SPEED * dt;

    // Miss – note passed below screen
    if (notes[i].y > SCREEN_H + NOTE_RADIUS) {
      notes[i].active = false;
      combo = 0;
      prevCombo = -1; // force redraw
    }
  }

  // ── 4. Handle button presses (hit detection) ──────────────────────
  for (int lane = 0; lane < NUM_LANES; lane++) {
    if (!btnJustPressed[lane]) continue;

    bool hit = false;
    int  bestIdx  = -1;
    float bestDist = HIT_WINDOW + 1;

    for (int i = 0; i < MAX_NOTES; i++) {
      if (!notes[i].active || notes[i].lane != lane) continue;
      float noteCY = notes[i].y + NOTE_RADIUS;
      float dist   = abs(noteCY - HIT_ZONE_Y);
      if (dist <= HIT_WINDOW && dist < bestDist) {
        bestDist = dist;
        bestIdx  = i;
      }
    }

    if (bestIdx >= 0) {
      clearNote(bestIdx);
      notes[bestIdx].active = false;
      score += 10 + combo;
      combo++;
      hit = true;
      flashHitZone(lane);
    }

    if (!hit) {
      combo = 0;
    }
  }

  for (int i = 0; i < MAX_NOTES; i++) {
    if (notes[i].active) drawNote(i);
  }

  for (int i = 0; i < NUM_LANES; i++) {
    if (btnState[i]) {
      tft.fillCircle(laneCX[i], HIT_ZONE_Y, HIT_ZONE_R, C_WHITE);
    } else {
      tft.fillCircle(laneCX[i], HIT_ZONE_Y, HIT_ZONE_R, C_BLACK);
      tft.drawCircle(laneCX[i], HIT_ZONE_Y, HIT_ZONE_R, LANE_COLORS[i]);
      tft.drawCircle(laneCX[i], HIT_ZONE_Y, HIT_ZONE_R - 1, LANE_COLORS[i]);
    }
  }

  updateScoreDisplay();

  if (beatmapIndex >= BEATMAP_LEN) {
    bool anyActive = false;
    for (int i = 0; i < MAX_NOTES; i++) {
      if (notes[i].active) { anyActive = true; break; }
    }
    if (!anyActive) {
      tft.fillScreen(C_BLACK);
      tft.setCursor(40, 180);
      tft.setTextColor(C_YELLOW); tft.setTextSize(3);
      tft.println("SONG CLEAR!");
      tft.setCursor(60, 230);
      tft.setTextColor(C_WHITE); tft.setTextSize(2);
      tft.print("Score: "); tft.println(score);
      while (true); // halt
    }
  }
}
