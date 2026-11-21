#include "moonUtils.h"
#include <pebble.h>

// 2000-01-01 12:00 UTC (J2000.0) as a unix timestamp
#define J2000_UNIX 946728000

// pixels of outline drawn around the lit part, roughly matching the sun's
// 3 pixel stroke, which is centered on its edge
#define MOON_OUTLINE 2

// dots in the dotted circle tracing the dark limb
#define MOON_LIMB_DOTS 12

// largest radius the pixel grid below has room for
#define MOON_MAX_RADIUS 12
#define MOON_GRID_HALF (MOON_MAX_RADIUS + MOON_OUTLINE)
#define MOON_GRID_SIZE (MOON_GRID_HALF * 2 + 1)

typedef enum {
  MOON_PX_NONE = 0,
  MOON_PX_LIMB_DOT,
  MOON_PX_OUTLINE,
  MOON_PX_LIT,
  MOON_PX_MARE,
} MoonPixel;

// A few of the large near-side maria, as seen from the northern hemisphere
// with north up. x/y/radius are fractions of the moon's radius (y is down).
// They're simplified into round blobs that stay legible at ~15 pixels across.
typedef struct {
  float x, y, r;
} Mare;

static const Mare MARIA[] = {
    {-0.30f, -0.34f, 0.26f}, // Imbrium
    {0.28f, -0.22f, 0.22f},  // Serenitatis / Tranquillitatis
    {-0.56f, 0.20f, 0.22f},  // Procellarum
    {0.64f, -0.36f, 0.12f},  // Crisium
    {-0.06f, 0.50f, 0.14f},  // Nubium
};
#define NUM_MARIA (sizeof(MARIA) / sizeof(MARIA[0]))

static double wrap_degrees(double deg) {
  long turns = (long)(deg / 360.0);
  deg -= turns * 360.0;
  if (deg < 0) {
    deg += 360.0;
  }
  return deg;
}

static double sin_deg(double deg) {
  int32_t angle = (int32_t)(wrap_degrees(deg) * TRIG_MAX_ANGLE / 360.0);
  return (double)sin_lookup(angle) / TRIG_MAX_RATIO;
}

static double cos_deg(double deg) {
  int32_t angle = (int32_t)(wrap_degrees(deg) * TRIG_MAX_ANGLE / 360.0);
  return (double)cos_lookup(angle) / TRIG_MAX_RATIO;
}

// no libm on the watch
static float sqrt_f(float x) {
  if (x <= 0) {
    return 0;
  }
  float guess = x > 1 ? x / 2 : 1;
  for (int i = 0; i < 8; i++) {
    guess = (guess + x / guess) / 2;
  }
  return guess;
}

static int round_f(float x) { return (int)(x < 0 ? x - 0.5f : x + 0.5f); }

float moon_get_elongation(time_t utcTime) {
  double days = (double)(utcTime - J2000_UNIX) / 86400.0;

  // mean elongation of the moon, mean anomaly of the sun and of the moon
  double D = wrap_degrees(297.8501921 + 12.19074912 * days);
  double M = wrap_degrees(357.5291092 + 0.98560028 * days);
  double Mp = wrap_degrees(134.9633964 + 13.06499295 * days);

  // largest periodic terms of the moon's longitude relative to the sun's
  double elongation = D + 6.289 * sin_deg(Mp) - 2.100 * sin_deg(M) +
                      1.274 * sin_deg(2 * D - Mp) + 0.658 * sin_deg(2 * D) +
                      0.214 * sin_deg(2 * Mp) - 0.110 * sin_deg(D);

  return (float)wrap_degrees(elongation);
}

int moon_get_ring_minute(int localMinuteOfDay, float elongation) {
  // the sky turns 360 degrees in 1440 minutes, so 4 minutes per degree
  int minute = localMinuteOfDay - (int)(elongation * 4.0f + 0.5f);
  minute %= 24 * 60;
  if (minute < 0) {
    minute += 24 * 60;
  }
  return minute;
}

void moon_draw(GContext *ctx, GPoint center, int radius, float elongation,
               ColorTheme *theme) {
  if (radius > MOON_MAX_RADIUS) {
    radius = MOON_MAX_RADIUS;
  }
  int half = radius + MOON_OUTLINE;

  // grid[y][x] for pixels at center + (x - MOON_GRID_HALF, y - MOON_GRID_HALF)
  uint8_t grid[MOON_GRID_SIZE][MOON_GRID_SIZE];
  memset(grid, MOON_PX_NONE, sizeof(grid));

  // The terminator is a half-ellipse: on each row of the disk, of half-width
  // w, it sits at x = w * cos(elongation). Waxing moons are lit on the right
  // of it, waning moons on the left (as seen from the northern hemisphere).
  bool waxing = elongation < 180.0f;
  float terminator = (float)cos_deg((double)elongation);

  // the lit disk is a touch smaller than radius so that, with its outline, it
  // comes out the same size as the sun
  float diskR2 = (float)(radius * radius - radius);

  for (int y = -radius; y <= radius; y++) {
    float w = sqrt_f(diskR2 - (float)(y * y));
    for (int x = -radius; x <= radius; x++) {
      if ((float)(x * x + y * y) > diskR2) {
        continue;
      }
      bool lit = waxing ? (x > w * terminator) : (x < -w * terminator);
      if (!lit) {
        continue;
      }

      uint8_t px = MOON_PX_LIT;
      for (unsigned int m = 0; m < NUM_MARIA; m++) {
        float dx = x - MARIA[m].x * radius;
        float dy = y - MARIA[m].y * radius;
        float mr = MARIA[m].r * radius;
        if (dx * dx + dy * dy <= mr * mr) {
          px = MOON_PX_MARE;
          break;
        }
      }
      grid[y + MOON_GRID_HALF][x + MOON_GRID_HALF] = px;
    }
  }

  // outline: any unlit pixel within MOON_OUTLINE of a lit one
  for (int y = -half; y <= half; y++) {
    for (int x = -half; x <= half; x++) {
      uint8_t *px = &grid[y + MOON_GRID_HALF][x + MOON_GRID_HALF];
      if (*px != MOON_PX_NONE) {
        continue;
      }
      for (int dy = -MOON_OUTLINE; dy <= MOON_OUTLINE && !*px; dy++) {
        for (int dx = -MOON_OUTLINE; dx <= MOON_OUTLINE; dx++) {
          int nx = x + dx, ny = y + dy;
          if (dx * dx + dy * dy > MOON_OUTLINE * MOON_OUTLINE ||
              nx < -radius || nx > radius || ny < -radius || ny > radius) {
            continue;
          }
          uint8_t n = grid[ny + MOON_GRID_HALF][nx + MOON_GRID_HALF];
          if (n == MOON_PX_LIT || n == MOON_PX_MARE) {
            *px = MOON_PX_OUTLINE;
            break;
          }
        }
      }
    }
  }

  // dotted circle around the whole disk, left showing only on the dark side
  float limbR = radius - 0.5f;
  for (int i = 0; i < MOON_LIMB_DOTS; i++) {
    int32_t angle = i * TRIG_MAX_ANGLE / MOON_LIMB_DOTS;
    int x = round_f(limbR * sin_lookup(angle) / TRIG_MAX_RATIO);
    int y = round_f(-limbR * cos_lookup(angle) / TRIG_MAX_RATIO);
    uint8_t *px = &grid[y + MOON_GRID_HALF][x + MOON_GRID_HALF];
    if (*px == MOON_PX_NONE) {
      *px = MOON_PX_LIMB_DOT;
    }
  }

  // paint the grid as horizontal runs of each color
  GColor colors[] = {
      GColorClear,
      PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite), // limb dots
      theme->sunStrokeColor,                           // outline
      GColorWhite,                                     // lit
      GColorLightGray,                                 // maria
  };

  for (int y = -half; y <= half; y++) {
    uint8_t *row = grid[y + MOON_GRID_HALF];
    int x = -half;
    while (x <= half) {
      uint8_t px = row[x + MOON_GRID_HALF];
      int start = x;
      while (x <= half && row[x + MOON_GRID_HALF] == px) {
        x++;
      }
      if (px == MOON_PX_NONE) {
        continue;
      }
      graphics_context_set_fill_color(ctx, colors[px]);
      graphics_fill_rect(
          ctx, GRect(center.x + start, center.y + y, x - start, 1), 0,
          GCornerNone);
    }
  }
}
