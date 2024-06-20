#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SYNTH_SAMPLE_RATE 44100

static bool synth_en = false;

static inline void synth_audio(uint16_t *buf, uint32_t count)
{
  static int phase = 0;
  if (!synth_en) {
    for (int i = 0; i < count; i++) buf[i] = 0;
  } else {
    // 441 Hz triangle
    for (int i = 0; i < count; i++) {
      phase += 1;
      if (phase >= 100) phase = 0;
      buf[i] = (phase >= 50 ? 100 - phase : phase) * 20;
    }
  }
}

static inline void synth_buttons(bool btn[12])
{
  synth_en = btn[1];
}
