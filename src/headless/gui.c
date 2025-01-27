#include "raylib.h"
#include "src/br_da.h"
#include "src/br_plot.h"
#include "src/br_gui_internal.h"

void br_gui_init_specifics_gui(br_plotter_t* br) {
  br_plot_instance_t plot = { 
    .kind = br_plot_instance_kind_2d,
    .resolution = { 1280, 720 },
    .graph_screen_rect = { 0, 0, 1280, 720 },
    .dd = (br_plot_instance_2d_t) {
      .zoom = { 1, 1},
      .offset = { 0 },
      .line_shader = br->shaders.line,
      .grid_shader = br->shaders.grid
    }
  };
  br_da_push_t(int, br->plots, plot);
}

void br_plotter_draw(br_plotter_t* br) {
  br_plot_instance_t* plot = &br->plots.arr[0];
  BeginDrawing();
  br_plotter_update_variables(br);
  help_draw_fps(0, 0);
  draw_grid_numbers(plot);
  smol_mesh_grid_draw(plot);
  points_groups_draw(br->groups, plot);
  ClearBackground(BLACK);
  EndDrawing();
}

void br_plot_instance_screenshot(br_plot_instance_t* br, points_groups_t groups, const char* path) {
  (void)br; (void)path; (void)groups;
  LOGI("Grabbing screenshot in headless, NOP\n");
  return;
}
