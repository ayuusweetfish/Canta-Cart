// gcc -DSOKOL_IMPL -DSTB_IMAGE_IMPLEMENTATION -x objective-c libs.h -c -O2
// gcc main.c -std=c99 libs.o -framework AppKit -framework OpenGL -framework AudioToolbox

#include "libs.h"

#include "canta_synth.h"
#include "silkscreen.png.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

static float audio_rms = 0;

static void audio_cb(float *buffer, int num_frames, int num_channels)
{
  int16_t ibuf[4096];
  assert(num_frames <= sizeof ibuf / sizeof ibuf[0]);

  synth_audio(ibuf, num_frames);

  float energy = 0;
  for (int i = 0; i < num_frames; i++) {
    float sample = (float)ibuf[i] / 65536;
    for (int j = 0; j < num_channels; j++)
      buffer[i * num_channels + j] = sample;
    energy += sample * sample;
  }
  audio_rms = audio_rms * 0.5f + (energy / num_frames) * 0.5f;
}

static sg_image load_image(const uint8_t *content, int content_len)
{
  int w, h, n_ch;
  uint8_t *data = stbi_load_from_memory(content, content_len, &w, &h, &n_ch, 4);
  assert(data != NULL && n_ch == 4);
  sg_image_desc image_desc = (sg_image_desc){
    .width = w,
    .height = h,
    .data = {
      .subimage = {{{ .ptr = data, .size = (size_t)(w * h * 4) }}},
    },
  };
  sg_image img = sg_make_image(&image_desc);
  stbi_image_free(data);
  return img;
}

static sg_image img_silkscreen;

static void init()
{
  sg_setup(&(sg_desc){
    .environment = sglue_environment(),
  });
  assert(sg_isvalid);

  sgp_setup(&(sgp_desc){ 0 });
  assert(sgp_is_valid());

  saudio_setup(&(saudio_desc){
    .stream_cb = audio_cb,
    .buffer_frames = 1024,
  });
  assert(saudio_sample_rate() == SYNTH_SAMPLE_RATE);

  img_silkscreen = load_image(silkscreen_png, silkscreen_png_len);
}

static bool buttons[12] = { false };

static inline void circle_line(float cx, float cy, float r)
{
  sgp_point p[49];
  int n = 48, ptr = 0;
  for (int i = 0; i < n; i++) {
    float th = (float)M_PI * 2 / n * i;
    float x = cx + r * cosf(th);
    float y = cy + r * sinf(th);
    p[ptr++] = (sgp_point){ .x = x, .y = y };
  }
  p[ptr++] = (sgp_point){ .x = cx + r, .y = cy };
  sgp_draw_lines_strip(p, ptr);
}

static inline void circle_fill(float cx, float cy, float r)
{
  sgp_point p[50];
  int n = 48, ptr = 0;
  for (int i = 0; i <= n / 2; i++) {
    float th = (float)M_PI * 2 / n * i;
    float dx = r * cosf(th);
    float dy = r * sinf(th);
    p[ptr++] = (sgp_point){ .x = cx + dx, .y = cy + dy };
    p[ptr++] = (sgp_point){ .x = cx - dx, .y = cy - dy };
  }
  sgp_draw_filled_triangles_strip(p, ptr);
}

static inline void circle_key(float cx, float cy, float r, bool fill)
{
  float a = (fill ? 1 : 0.2);
  sgp_set_color(0.12 + 0.88 * a, 0.12 + 0.86 * a, 0.12 + 0.855 * a, 1);
  circle_fill(cx, cy, r);
}

static inline void rrect_fill(float cx, float cy, float w, float h, float r)
{
  sgp_point p[54];
  int n = 48;
  for (int i = 0; i <= n / 4; i++) {
    float th = (float)M_PI * 2 / n * i;
    float dx = r * cosf(th);
    float dy = r * sinf(th);
    p[i * 2 + 0] = (sgp_point){ .x = cx + (w/2 - r + dx), .y = cy + (h/2 - r + dy) };
    p[i * 2 + 1] = (sgp_point){ .x = cx - (w/2 - r + dx), .y = cy - (h/2 - r + dy) };
    p[n + 3 - (i * 2 + 0)] = (sgp_point){ .x = cx + (w/2 - r + dx), .y = cy - (h/2 - r + dy) };
    p[n + 3 - (i * 2 + 1)] = (sgp_point){ .x = cx - (w/2 - r + dx), .y = cy + (h/2 - r + dy) };
  }
  p[n + 4] = p[1];
  p[n + 5] = p[0];
  sgp_draw_filled_triangles_strip(p, n + 6);
}

static inline void rrect_key(float cx, float cy, float w, float h, float r, bool fill)
{
  float a = (fill ? 1 : 0.2);
  sgp_set_color(0.12 + 0.88 * a, 0.12 + 0.86 * a, 0.12 + 0.855 * a, 1);
  rrect_fill(cx, cy, w, h, r);
}

static void frame()
{
  int w = sapp_width(), h = sapp_height();
  sgp_begin(w, h);
  sgp_viewport(0, 0, w, h);
  sgp_project(0, w, 0, h);
  sgp_set_color(0.12f, 0.12f, 0.12f, 1.0f);
  sgp_clear();

  // Silkscreen
  sgp_set_blend_mode(SGP_BLENDMODE_BLEND);
  sgp_set_color(1.0, 0.98, 0.975, 1);
  sgp_set_image(0, img_silkscreen);
  sgp_draw_filled_rect(0, 0, w, h);
  sgp_reset_image(0);
  sgp_reset_blend_mode();

  // -4.5 dB: 0.5
  // -3 dB: 1
  float a = log10f(audio_rms);
  a = (a + 6) / 3;
  if (a > 1) a = 1; else if (a < 0) a = 0;
  sgp_set_color(0.12 + 0.88 * a, 0.12 + 0.86 * a, 0.12 + 0.855 * a, 1);
  circle_fill(428, 207, 100);
  sgp_set_color(1, 0.98, 0.975, 1);
  circle_line(428, 207, 100);

  circle_key( 67.9, 250, 60, buttons[10]);
  circle_key(788.1, 250, 60, buttons[11]);
  for (int i = 0; i < 10; i++)
    rrect_key(45.4 + 85 * i, 431.9, 75, 200, 22.5, buttons[i]);

  sg_begin_pass(&(sg_pass){ .swapchain = sglue_swapchain() });
  sgp_flush();
  sgp_end();
  sg_end_pass();
  sg_commit();
}

static void cleanup()
{
  puts("cleanup");
  sgp_shutdown();
  sg_shutdown();
}

static void event(const sapp_event *ev)
{
  static const uint8_t keys[SAPP_MAX_KEYCODES] = {
    [SAPP_KEYCODE_1] = 12 + 0,
    [SAPP_KEYCODE_2] = 12 + 1,
    [SAPP_KEYCODE_3] = 12 + 2,
    [SAPP_KEYCODE_4] = 12 + 3,
    [SAPP_KEYCODE_5] = 12 + 4,
    [SAPP_KEYCODE_6] = 12 + 5,
    [SAPP_KEYCODE_7] = 12 + 6,
    [SAPP_KEYCODE_8] = 12 + 7,
    [SAPP_KEYCODE_9] = 12 + 8,
    [SAPP_KEYCODE_0] = 12 + 9,
    [SAPP_KEYCODE_GRAVE_ACCENT] = 12 + 10,
    [SAPP_KEYCODE_MINUS] = 12 + 11,
  };

  if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
    if (ev->key_code == SAPP_KEYCODE_Q && (ev->modifiers & SAPP_MODIFIER_SUPER)) {
      sapp_quit();
      return;
    }
    if (keys[ev->key_code] != 0) {
      buttons[keys[ev->key_code] - 12] = true;
      synth_buttons(buttons);
    }
  } else if (ev->type == SAPP_EVENTTYPE_KEY_UP) {
    if (keys[ev->key_code] != 0) {
      buttons[keys[ev->key_code] - 12] = false;
      synth_buttons(buttons);
    }
  }
}

#include <time.h>
static inline void perf_test()
{
  for (uint32_t phase = 0; phase < 16; phase++)
    printf("%08x %d\n", phase << 28, (int)synth_table(phase << 28));

  bool btns[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
  synth_buttons(btns);
  for (int it = 0; it < 10; it++) {
    clock_t t0 = clock();
    int16_t buf[512];
    for (int i = 0; i < 100000; i++) {
      synth_audio(buf, sizeof buf / sizeof(int16_t));
    }
    clock_t t1 = clock();
    printf("time: %.4f\n", (float)(t1 - t0) / CLOCKS_PER_SEC);
  }
}

sapp_desc sokol_main(int argc, char* argv[])
{
  // perf_test();
  return (sapp_desc) {
    .width = 856,
    .height = 540,
    .init_cb = init,
    .frame_cb = frame,
    .cleanup_cb = cleanup,
    .event_cb = event,
  };
}
