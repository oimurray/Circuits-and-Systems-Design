#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/pgmspace.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_DC   9
#define OLED_CS   10
#define OLED_RST  8

#define PWM_PIN 3          // Sine output (PWM)
#define TABLE_SIZE 256

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);

// ===================== SINE TABLE =====================
const uint8_t sineTable[TABLE_SIZE] PROGMEM = {
  128,131,134,137,140,143,146,149,152,155,158,162,165,168,171,174,
  177,180,183,186,189,192,195,198,201,204,207,210,213,216,218,221,
  224,227,230,233,235,238,241,243,246,249,251,253,255,253,251,249,
  246,243,241,238,235,233,230,227,224,221,218,216,213,210,207,204,
  201,198,195,192,189,186,183,180,177,174,171,168,165,162,158,155,
  152,149,146,143,140,137,134,131,128,124,121,118,115,112,109,106,
  103,100,97,93,90,87,84,81,78,75,72,69,66,63,60,57,54,51,
  48,45,42,39,37,34,31,28,25,23,20,17,15,12,9,7,
  5,7,9,12,15,17,20,23,25,28,31,34,37,39,42,45,
  48,51,54,57,60,63,66,69,72,75,78,81,84,87,90,93,
  97,100,103,106,109,112,115,118,121,124
};

volatile uint16_t samp[256];
volatile uint16_t widx = 0;
volatile bool bufFull = false;

const uint16_t N = 256;
const uint16_t TRIG_LEVEL = 512;
const bool TRIG_RISING = true;

// ===================== ADC ISR =====================
ISR(ADC_vect) {
  if (bufFull) return;
  samp[widx++] = ADC;
  if (widx >= N) bufFull = true;
}

static inline void adc_start_freerun_A0() {
  ADMUX  = (1 << REFS0); // AVcc ref, A0
  ADCSRB = 0;
  ADCSRA = (1 << ADEN)  | (1 << ADATE) | (1 << ADIE) |
           (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  DIDR0  = (1 << ADC0D);

  widx = 0;
  bufFull = false;
  ADCSRA |= (1 << ADSC);
}

// ===================== TRIGGER =====================
static int find_trigger_index(const uint16_t *x, uint16_t n) {
  for (uint16_t i = 1; i < n; i++) {
    uint16_t a = x[i - 1], b = x[i];
    if (TRIG_RISING) {
      if (a < TRIG_LEVEL && b >= TRIG_LEVEL) return i;
    } else {
      if (a > TRIG_LEVEL && b <= TRIG_LEVEL) return i;
    }
  }
  return 0;
}

// ===================== DRAW =====================
static void draw_waveform(const uint16_t *x, uint16_t n) {
  uint16_t xmin = 1023, xmax = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (x[i] < xmin) xmin = x[i];
    if (x[i] > xmax) xmax = x[i];
  }
  if (xmax == xmin) xmax = xmin + 1;

  int t0 = find_trigger_index(x, n);

  display.clearDisplay();

  for (int px = 0; px < SCREEN_WIDTH; px++) {
    uint16_t idx = (t0 + (uint32_t)px * (n - 1) / (SCREEN_WIDTH - 1)) % n;
    uint16_t v = x[idx];

    int y = (int)((uint32_t)(v - xmin) * (SCREEN_HEIGHT - 1) / (xmax - xmin));
    y = (SCREEN_HEIGHT - 1) - y;

    if (px > 0) {
      uint16_t idxP = (t0 + (uint32_t)(px - 1) * (n - 1) / (SCREEN_WIDTH - 1)) % n;
      uint16_t vP = x[idxP];
      int yP = (int)((uint32_t)(vP - xmin) * (SCREEN_HEIGHT - 1) / (xmax - xmin));
      yP = (SCREEN_HEIGHT - 1) - yP;
      display.drawLine(px - 1, yP, px, y, SSD1306_WHITE);
    }
  }

  display.display();
}

// ===================== SETUP =====================
void setup() {
  pinMode(PWM_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    while (1) {}
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Scope A0");
  display.display();

  cli();
  adc_start_freerun_A0();
  sei();
}

// ===================== LOOP =====================
void loop() {
  static uint16_t local[N];

  // ----- SINE GENERATOR (0.5 Hz) -----
  static uint16_t sineIndex = 0;
  static uint32_t lastUpdate = 0;

  if (micros() - lastUpdate >= 7800) { // 2 s / 256
    lastUpdate += 7800;
    uint8_t v = pgm_read_byte(&sineTable[sineIndex]);
    analogWrite(PWM_PIN, v);
    sineIndex = (sineIndex + 1) % TABLE_SIZE;
  }

  // ----- SCOPE UPDATE -----
  if (bufFull) {
    cli();
    for (uint16_t i = 0; i < N; i++) local[i] = samp[i];
    widx = 0;
    bufFull = false;
    sei();

    draw_waveform(local, N);
  }
}
