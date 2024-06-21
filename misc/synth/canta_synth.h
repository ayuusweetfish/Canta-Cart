#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYNTH_SAMPLE_RATE 44100

#define SYNTH_ATTACK_INCR   (int)((1 << 15) / (SYNTH_SAMPLE_RATE * 0.025))
#define SYNTH_RELEASE_INCR  (int)((1 << 15) / (SYNTH_SAMPLE_RATE * 0.075))

static int8_t base = 0;

static inline double freq_for_note(int8_t n, int8_t transpose)
{
  int octave = (n < 0 ? (n - 6) / 7 : n / 7);
  int degree = n - octave * 7;
  static const uint8_t scale[7] = {0, 2, 4, 5, 7, 9, 11};
  int midi_pitch = 71 + (octave * 12 + scale[degree]) + transpose;
  return 440.0 * pow(2, (midi_pitch - 69) / 12.0);
}

static struct {
  uint32_t freq;
  uint32_t phase;

  uint16_t state; // 0: idle; 1: attack/decay; 2: sustain; 3: release
  uint16_t time;
} keys[10];

static bool last_btn[12] = { 0 };
static bool transp_used[2];

static inline int16_t synth_table(uint32_t phase)
{
  return (phase < (1u << 31) ?
      (int32_t)-(1u << 31) + (int32_t)phase * 2 :
      (int32_t)((1u << 31) - 1) - (int32_t)(phase - (1u << 31)) * 2
    ) >> 22;
}

static inline void synth_audio(int16_t *buf, uint32_t count)
{
  memset(buf, 0, count * sizeof(int16_t));
  for (int i = 0; i < 10; i++) if (keys[i].state != 0) {
    for (int j = 0; j < count; j++) {
      keys[i].phase += keys[i].freq;  // Silent overflow
      int16_t value = synth_table(keys[i].phase);
      if (__builtin_expect(keys[i].state == 1, 0)) {
        keys[i].time += SYNTH_ATTACK_INCR;
        if (keys[i].time >= (1 << 15)) {
          keys[i].state = 2;
        } else {
          value = ((int32_t)value * keys[i].time) >> 15;
        }
      } else if (__builtin_expect(keys[i].state == 3, 0)) {
        keys[i].time += SYNTH_RELEASE_INCR;
        if (keys[i].time >= (1 << 15)) {
          keys[i].state = 0;
          value = 0;
          break;
        } else {
          value = ((int32_t)value * ((1 << 15) - keys[i].time)) >> 15;
        }
      }
      buf[j] += value;
    }
  }
}

static inline void synth_buttons(bool btn[12])
{
  for (int i = 11; i >= 0; i--) {
    if (btn[i] && !last_btn[i]) {
      // Press
      if (i < 10) {
        int transpose = 0;
        if (btn[10]) { transpose -= 1; transp_used[0] = true; }
        if (btn[11]) { transpose += 1; transp_used[1] = true; }
        double f = freq_for_note(base + i, transpose);
        // Period = f_s/f samples = 2^32 in lookup table
        // Increment for each sample = 2^32 / (f_s/f)
        keys[i].freq = (uint32_t)(4294967296.0 * f / SYNTH_SAMPLE_RATE);
        keys[i].phase = 0;
        keys[i].state = 1;
        keys[i].time = 0;
      } else {
        transp_used[i - 10] = false;
      }
    } else if (!btn[i] && last_btn[i]) {
      // Release
      if (i < 10) {
        keys[i].state = 3;
        keys[i].time = 0;
      } else {
        // If no note has been pressed during the hold,
        // carry out a global transposition
        if (!transp_used[i - 10]) {
          base += (i == 10 ? -1 : +1);
          if (base < -18) base = -18;
          if (base >  14) base =  14;
        }
      }
    }
    last_btn[i] = btn[i];
  }
}
