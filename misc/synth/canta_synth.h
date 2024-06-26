#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef SYNTH_SAMPLE_RATE
  #define SYNTH_SAMPLE_RATE 44100
#endif
#ifndef SYNTH_EXTRA_SHIFT
  #define SYNTH_EXTRA_SHIFT 1
#endif

#define SYNTH_ATTACK_INCR   (int)((1 << 15) / (SYNTH_SAMPLE_RATE * 0.025))
#define SYNTH_RELEASE_INCR  (int)((1 << 15) / (SYNTH_SAMPLE_RATE * 0.075))

static int8_t key_base = 0;
static int8_t scale_base = 0;

// MIDI note:
// Max. 71 + (n + scale_base) (+23 scale degrees, +40) + key_base (+12) + transpose (+1) = 124
// Min. 71 + (n + scale_base) (-18 scale degrees, -31) + key_base (-12) + transpose (-1) = 27

// Period = f_s/f samples = 2^32 in lookup table
// Increment for each sample = 2^32 / (f_s/f)
// note_freq[i] =
//   4294967296.0 * (440.0 * pow(2, (i - 69) / 12.0)) / SYNTH_SAMPLE_RATE
static const uint32_t note_freq[128] = {
  // print('\n'.join('  (uint32_t)(%.4f / SYNTH_SAMPLE_RATE + 0.5),' % (4294967296.0 * (440.0 * (2 ** ((i - 69) / 12.0)))) for i in range(128)))
  (uint32_t)(35114788961.3620 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(37202822970.7782 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(39415017943.5217 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(41758756874.6707 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(44241861775.0361 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(46872619776.3960 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(49659810789.0272 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(52612736803.8398 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(55741252936.9073 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(59055800320.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(62567440946.8918 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(66287894591.7365 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(70229577922.7240 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(74405645941.5564 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(78830035887.0435 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(83517513749.3414 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(88483723550.0722 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(93745239552.7919 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(99319621578.0543 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(105225473607.6797 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(111482505873.8147 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(118111600640.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(125134881893.7837 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(132575789183.4730 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(140459155845.4479 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(148811291883.1128 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(157660071774.0869 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(167035027498.6827 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(176967447100.1444 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(187490479105.5839 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(198639243156.1087 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(210450947215.3594 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(222965011747.6293 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(236223201280.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(250269763787.5674 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(265151578366.9460 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(280918311690.8959 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(297622583766.2255 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(315320143548.1738 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(334070054997.3655 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(353934894200.2888 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(374980958211.1677 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(397278486312.2173 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(420901894430.7188 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(445930023495.2587 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(472446402560.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(500539527575.1349 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(530303156733.8922 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(561836623381.7917 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(595245167532.4510 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(630640287096.3478 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(668140109994.7310 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(707869788400.5775 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(749961916422.3354 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(794556972624.4347 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(841803788861.4374 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(891860046990.5173 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(944892805120.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1001079055150.2698 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1060606313467.7844 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1123673246763.5835 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1190490335064.9021 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1261280574192.6956 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1336280219989.4619 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1415739576801.1550 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1499923832844.6709 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1589113945248.8694 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1683607577722.8748 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1783720093981.0347 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(1889785610240.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2002158110300.5396 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2121212626935.5688 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2247346493527.1670 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2380980670129.8042 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2522561148385.3911 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2672560439978.9238 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2831479153602.3101 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(2999847665689.3418 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(3178227890497.7388 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(3367215155445.7495 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(3567440187962.0693 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(3779571220480.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(4004316220601.0791 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(4242425253871.1377 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(4494692987054.3340 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(4761961340259.6084 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(5045122296770.7822 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(5345120879957.8477 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(5662958307204.6201 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(5999695331378.6836 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(6356455780995.4775 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(6734430310891.4990 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(7134880375924.1387 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(7559142440960.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(8008632441202.1582 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(8484850507742.2734 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(8989385974108.6680 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(9523922680519.2168 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(10090244593541.5625 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(10690241759915.6953 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(11325916614409.2422 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(11999390662757.3672 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(12712911561990.9551 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(13468860621783.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(14269760751848.2773 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(15118284881920.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(16017264882404.3164 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(16969701015484.5469 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(17978771948217.3359 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(19047845361038.4336 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(20180489187083.1250 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(21380483519831.3906 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(22651833228818.4844 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(23998781325514.7344 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(25425823123981.9102 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(26937721243566.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(28539521503696.5547 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(30236569763840.0000 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(32034529764808.6250 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(33939402030969.1094 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(35957543896434.6719 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(38095690722076.8672 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(40360978374166.2656 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(42760967039662.7812 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(45303666457636.9531 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(47997562651029.4766 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(50851646247963.8203 / SYNTH_SAMPLE_RATE + 0.5),
  (uint32_t)(53875442487131.9844 / SYNTH_SAMPLE_RATE + 0.5),
};

static inline uint32_t freq_for_note(int8_t n, int8_t key_base, int8_t transpose)
{
  int octave = (n < 0 ? (n - 6) / 7 : n / 7);
  int degree = n - octave * 7;
  static const uint8_t scale[7] = {0, 2, 4, 5, 7, 9, 11};
  int midi_pitch = 71 + key_base + (octave * 12 + scale[degree]) + transpose;
  printf("%d\n", midi_pitch);
  return note_freq[midi_pitch];
}

static struct {
  uint32_t freq;
  uint32_t phase;

  uint16_t state; // 0: idle; 1: attack/decay; 2: sustain; 3: release
  uint16_t time;
} keys[10];

static bool last_btn[12] = { 0 };

// Whether a note has been pressed during the time when the transpose key is held
static bool transp_used[2];

static inline int16_t synth_table(uint32_t phase)
{
  return (phase < (1u << 31) ?
      (int32_t)((1u << 31) + phase * 2) :
      (int32_t)(((1u << 31) - 1) - (phase - (1u << 31)) * 2)
    ) >> (16 /* Max. range */ + 4 /* Avoid clipping with polyphony 10 */ + SYNTH_EXTRA_SHIFT);
}

static inline void synth_audio(int16_t *buf, uint32_t count)
{
  memset(buf, 0, count * sizeof(int16_t));
  for (int i = 0; i < 10; i++) if (keys[i].state != 0) {
    int j = 0;
    while (j < count && keys[i].state != 0) {
      if (__builtin_expect(keys[i].state == 1, 0)) {
        for (; j < count; j++) {
          int16_t value = synth_table(keys[i].phase += keys[i].freq);
          keys[i].time += SYNTH_ATTACK_INCR;
          if (__builtin_expect(keys[i].time >= (1 << 15), 0)) {
            buf[j] += value;
            keys[i].state = 2;
            j++;
            break;
          } else {
            buf[j] += ((int32_t)value * keys[i].time) >> 15;
          }
        }
      } else if (__builtin_expect(keys[i].state == 3, 0)) {
        for (; j < count; j++) {
          int16_t value = synth_table(keys[i].phase += keys[i].freq);
          keys[i].time += SYNTH_RELEASE_INCR;
          if (__builtin_expect(keys[i].time >= (1 << 15), 0)) {
            keys[i].state = 0;
            j++;
            break;
          } else {
            buf[j] += ((int32_t)value * ((1 << 15) - keys[i].time)) >> 15;
          }
        }
      } else {
        for (; j < count; j++) {
          int16_t value = synth_table(keys[i].phase += keys[i].freq);
          buf[j] += value;
        }
      }
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
        keys[i].freq = freq_for_note(scale_base + i, key_base, transpose);
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
          if (btn[i ^ 1]) {
            // Key change
            key_base += (i == 10 ? -1 : +1);
            if (key_base < -12) key_base = -12;
            if (key_base >  12) key_base =  12;
            transp_used[(i - 10) ^ 1] = true;
          } else {
            // Scale base change
            scale_base += (i == 10 ? -1 : +1);
            if (scale_base < -18) scale_base = -18;
            if (scale_base >  14) scale_base =  14;
          }
        }
      }
    }
    last_btn[i] = btn[i];
  }
}
