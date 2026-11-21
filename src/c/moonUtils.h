#pragma once
#include <pebble.h>
#include "settings.h"

/*
 * Lunar phase tracking: where the moon sits on the 24 hour ring, and how to
 * draw its current phase.
 */

/**
 * The moon's elongation from the sun in degrees [0, 360): 0 is new moon, 90
 * first quarter, 180 full moon, 270 last quarter. Uses the main periodic terms
 * from Meeus (Astronomical Algorithms, ch. 47-48), good to a few hours of
 * phase timing.
 */
float moon_get_elongation(time_t utcTime);

/**
 * The minute of the (local) day at which the sun would sit where the moon is
 * now. The moon trails the sun across the sky by its elongation, so it's
 * drawn that far behind the sun on the ring: a full moon is opposite the
 * sun, a new moon hides right behind it.
 */
int moon_get_ring_minute(int localMinuteOfDay, float elongation);

/**
 * Draws the moon at center with the given radius (matching the sun's
 * SUN_DIAMETER): the lit part in white with a few grey maria so a full moon
 * can't be mistaken for the sun, an outline in the sun's stroke color around
 * the lit part, and a dotted circle tracing the unlit limb.
 */
void moon_draw(GContext *ctx, GPoint center, int radius, float elongation,
               ColorTheme *theme);
