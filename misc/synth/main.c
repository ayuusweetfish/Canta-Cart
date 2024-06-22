// gcc -O2 -std=c99 main.c

// On macOS:
// gcc -O2 -std=c99 -x objective-c main.c -framework AppKit -framework OpenGL -framework AudioToolbox

// During development:
// gcc -std=c99 -DONLY_LIBS_IMPL -x objective-c main.c -c -O2 -o libs.o
// gcc -std=c99 -DNO_LIBS_IMPL main.c libs.o -framework AppKit -framework OpenGL -framework AudioToolbox

// Emscripten (SDK version 2.0.0):
// emcc -O2 -std=c99 main.c -sFULL_ES3 -o web/canta-cart.js
// Touch-end event fix, ref. emscripten-core/emscripten#5012, SO /q/52265891
// perl -pi -w -e 's/(var et=e.touches;for\(var i=0;i<et.length;\+\+i\)\{var touch=et\[i\];touches\[touch.identifier\]=touch)\}/$1;touch.isChanged=0}/' web/canta-cart.js

// Clean:
// rm a.out libs.o web/canta-cart.{js,wasm}

// ============ Libraries ============
// Define NO_LIBS_IMPL for a statically linked (rather than amalgamated) build
#ifndef NO_LIBS_IMPL
  #define SOKOL_IMPL
  #define STB_IMAGE_IMPLEMENTATION
#endif

#ifdef __EMSCRIPTEN__
  #define SOKOL_GLES3
#else
  #define SOKOL_GLCORE
#endif

#include "sokol_app.h"
#include "sokol_audio.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"

// Workaround for minor incompatibility (ref. edubart/sokol_gp#32)
#define SG_BACKEND_GLCORE33 SG_BACKEND_GLCORE
#include "sokol_gp.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#include "stb_image.h"
// ===================================

#ifndef ONLY_LIBS_IMPL

#include "canta_synth.h"
#include "silkscreen.png.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384
#endif

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

struct pointer {
  uintptr_t id;
  float x, y;
};

static inline bool pt_in_btn(int i, float x, float y)
{
  if (i < 10) {
    float cx = 45.4 + 85 * i;
    float cy = 431.9;
    float w = 75;
    float h = 200;
    return x >= cx - w/2 && x <= cx + w/2 &&
           y >= cy - h/2 && y <= cy + h/2;
  } else {
    float cx = (i == 10 ? 67.9 : 788.1);
    float cy = 250;
    float r = 60;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
  }
}
static inline void pts_event(const struct pointer *pts, int n_pts)
{
/*
  printf("n=%d", n_pts);
  for (int i = 0; i < n_pts; i++) printf(" (%d %d)", (int)pts[i].x, (int)pts[i].y);
  putchar('\n');
*/
  for (int j = 0; j < 12; j++) {
    buttons[j] = false;
    for (int i = 0; i < n_pts; i++)
      if (pt_in_btn(j, pts[i].x, pts[i].y)) { buttons[j] = true; break; }
  }
  synth_buttons(buttons);
}

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
  sgp_project(0, 856, 0, 540);
  sgp_set_color(0.12f, 0.12f, 0.12f, 1.0f);
  sgp_clear();

  // Silkscreen
  sgp_set_blend_mode(SGP_BLENDMODE_BLEND);
  sgp_set_color(1.0, 0.98, 0.975, 1);
  sgp_set_image(0, img_silkscreen);
  sgp_draw_filled_rect(0, 0, 856, 540);
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

  // Pointer events
  struct pointer pts[SAPP_MAX_TOUCHPOINTS] = {{ 0 }};
  static bool has_touch = false;
  float sx = 856.0f / ev->window_width;
  float sy = 540.0f / ev->window_height;
  if (!has_touch &&
      (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
       (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE && (ev->modifiers & SAPP_MODIFIER_LMB)))) {
    pts[0] = (struct pointer){ .id = 0, .x = ev->mouse_x * sx, .y = ev->mouse_y * sy };
    pts_event(pts, 1);
  } else if (!has_touch && ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
    pts_event(pts, 0);
  } else if (
    ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN ||
    ev->type == SAPP_EVENTTYPE_TOUCHES_MOVED ||
    ev->type == SAPP_EVENTTYPE_TOUCHES_ENDED ||
    ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED
  ) {
    has_touch = true;
    assert(ev->num_touches < SAPP_MAX_TOUCHPOINTS);
    int n_pts = 0;
    for (int i = 0; i < ev->num_touches; i++) {
      if ((ev->type == SAPP_EVENTTYPE_TOUCHES_ENDED ||
           ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED) &&
          ev->touches[i].changed)
        continue;
      pts[n_pts++] = (struct pointer){
        .id = ev->touches[i].identifier,
        .x = ev->touches[i].pos_x * sx,
        .y = ev->touches[i].pos_y * sy,
      };
    }
    pts_event(pts, n_pts);
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

#endif  // #ifndef ONLY_LIBS_IMPL
