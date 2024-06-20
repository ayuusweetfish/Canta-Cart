// gcc -DSOKOL_IMPL -DSOKOL_GLCORE -x objective-c sokol_app.h -x c sokol_gfx.h -x c sokol_audio.h -c

// gcc -DSOKOL_IMPL -x objective-c libs.h -c -O2
// gcc main.c -std=c99 libs.o -framework AppKit -framework OpenGL -framework AudioToolbox

#include "libs.h"

#include <assert.h>
#include <stdio.h>

static void init()
{
  sg_setup(&(sg_desc){
    .environment = sglue_environment(),
  });
  assert(sg_isvalid);

  sgp_setup(&(sgp_desc){ 0 });
  assert(sgp_is_valid());
}

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
  if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
    if (ev->key_code == SAPP_KEYCODE_Q && (ev->modifiers & SAPP_MODIFIER_SUPER)) {
      sapp_quit();
      return;
    }
  } else if (ev->type == SAPP_EVENTTYPE_KEY_UP) {
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
