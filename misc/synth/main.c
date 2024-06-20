// gcc -DSOKOL_IMPL -x objective-c libs.h -c -O2
// gcc main.c -std=c99 libs.o -framework AppKit -framework OpenGL -framework AudioToolbox

#include "libs.h"

#include "canta_synth.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static void audio_cb(float *buffer, int num_frames, int num_channels)
{
  uint16_t ibuf[4096];
  assert(num_frames <= sizeof ibuf / sizeof ibuf[0]);

  synth_audio(ibuf, num_frames);
  for (int i = 0; i < num_frames; i++) {
    float sample = (float)ibuf[i] / 65536;
    for (int j = 0; j < num_channels; j++)
      buffer[i * num_channels + j] = sample;
  }
}

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
}

static bool buttons[12] = { false };

static void frame()
{
  int w = sapp_width(), h = sapp_height();
  sgp_begin(w, h);
  sgp_viewport(0, 0, w, h);
  sgp_project(-(float)w / h, (float)w / h, -1.0f, 1.0f);
  sgp_set_color(0.12f, 0.12f, 0.12f, 1.0f);
  sgp_clear();

  sgp_set_color(1, 0.98, 0.975, 1);
  sgp_draw_line(-1, 0, 1, 0);

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
    [SAPP_KEYCODE_GRAVE_ACCENT] = 12 + 0,
    [SAPP_KEYCODE_1] = 12 + 1,
    [SAPP_KEYCODE_2] = 12 + 2,
    [SAPP_KEYCODE_3] = 12 + 3,
    [SAPP_KEYCODE_4] = 12 + 4,
    [SAPP_KEYCODE_5] = 12 + 5,
    [SAPP_KEYCODE_6] = 12 + 6,
    [SAPP_KEYCODE_7] = 12 + 7,
    [SAPP_KEYCODE_8] = 12 + 8,
    [SAPP_KEYCODE_9] = 12 + 9,
    [SAPP_KEYCODE_0] = 12 + 10,
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

sapp_desc sokol_main(int argc, char* argv[])
{
  return (sapp_desc) {
    .width = 720,
    .height = 480,
    .init_cb = init,
    .frame_cb = frame,
    .cleanup_cb = cleanup,
    .event_cb = event,
  };
}
