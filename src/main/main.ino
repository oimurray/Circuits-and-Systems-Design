#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_DC   9
#define OLED_CS   10
#define OLED_RST  8

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);

const uint16_t N = 256;
volatile uint16_t samp[N];
volatile uint16_t widx = 0;
volatile bool bufFull = false;

const uint16_t TRIG_LEVEL = 512;
const bool TRIG_RISING = true;

ISR(ADC_vect) {
  if (bufFull) return;
  samp[widx++] = ADC;
  if (widx >= N) bufFull = true;
}

static inline void adc_start_freerun_A0() {
  // AVcc ref, channel ADC0 (A0)
  ADMUX  = (1 << REFS0) | 0;
  ADCSRB = 0; // free-run
  ADCSRA = (1 << ADEN)  | (1 << ADATE) | (1 << ADIE) |
           (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // prescaler 128
  DIDR0  = (1 << ADC0D);

  widx = 0;
  bufFull = false;
  ADCSRA |= (1 << ADSC);
}

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

    if (px == 0) {
      display.drawPixel(px, y, SSD1306_WHITE);
    } else {
      uint16_t idxPrev = (t0 + (uint32_t)(px - 1) * (n - 1) / (SCREEN_WIDTH - 1)) % n;
      uint16_t vPrev = x[idxPrev];
      int yPrev = (int)((uint32_t)(vPrev - xmin) * (SCREEN_HEIGHT - 1) / (xmax - xmin));
      yPrev = (SCREEN_HEIGHT - 1) - yPrev;
      display.drawLine(px - 1, yPrev, px, y, SSD1306_WHITE);
    }
  }

  display.display();
}

void setup() {
  // OLED init (SPI)
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

void loop() {
  static uint16_t local[N];

  if (bufFull) {
    cli();
    for (uint16_t i = 0; i < N; i++) local[i] = samp[i];
    widx = 0;
    bufFull = false;
    sei();

    draw_waveform(local, N);
  }
}
