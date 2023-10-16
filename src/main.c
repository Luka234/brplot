#include "plotter.h"

#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define WIDTH 1280
#define HEIGHT 720

context_t context;

int main_gui(graph_values_t* gv) {
  while(!WindowShouldClose()) {
    BeginDrawing();
      ClearBackground(BLACK);
      if (gv->state == plotter_state_default) graph_draw(gv);
      else if (gv->state == plotter_state_saving_file) file_saver_draw(gv->fs);
    EndDrawing();
  }
  return 0;
}

#ifdef ZIG
int _main(void) {
#else
void zig_print_stacktrace(void) { exit(-1); }
int main(void) {
#endif
  context = (context_t) {
    .debug_bounds = false
  };
#ifdef RELEASE
  SetTraceLogLevel(LOG_ERROR);
#endif
  SetWindowState(FLAG_MSAA_4X_HINT);
  InitWindow(WIDTH, HEIGHT, "rlplot");
  SetWindowState(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
  graph_values_t* gv = malloc(sizeof(graph_values_t));
  graph_init(gv, WIDTH, HEIGHT);
#ifndef RELEASE
  start_refreshing_shaders(gv);
#endif
  read_input_main(gv);
  main_gui(gv);

  // Clean up
  read_input_stop();
  graph_free(gv);
  free(gv);
  CloseWindow();
  return 0;
} 

