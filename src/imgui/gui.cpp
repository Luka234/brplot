#include "src/br_plot.h"
#include "src/br_gui_internal.h"
#include "src/br_da.h"
#include "imgui_extensions.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "GLFW/glfw3.h"

#include <cstdlib>

#define DEFAULT_INI "" \
"[Window][DockSpaceViewport_11111111]\n" \
"Size=1280,720\n" \
"\n" \
"[Window][Info]\n" \
"Pos=1007,0\n" \
"DockId=0x00000002,1\n" \
"\n" \
"[Window][Settings]\n" \
"Pos=1007,0\n" \
"DockId=0x00000002,0\n" \
"\n" \
"[Window][Plot]\n" \
"Pos=0,0\n" \
"Size=1005,720\n" \
"DockId=0x00000001,0\n" \
"\n" \
"[Docking][Data]\n" \
"DockSpace   ID=0x8B93E3BD Window=0xA787BDB4 Pos=0,0 Size=1280,720 Split=X\n" \
"  DockNode  ID=0x00000001 Parent=0x8B93E3BD SizeRef=1005,720\n" \
"  DockNode  ID=0x00000002 Parent=0x8B93E3BD SizeRef=273,720\n"

static void br_plot_instance_screenshot_imgui(br_plot_instance_t* br, points_groups_t groups, char* path);

static int screenshot_file_save = 0;
static struct br_file_saver_s* fs = nullptr;

static ImVec4 clear_color = ImVec4(.0f, .0f, .0f, 1.00f);
static float padding = 50.f;

static GLFWwindow* ctx;

extern "C" void br_gui_init_specifics_gui(br_plotter_t* br) {
  br_plot_instance_t plot;
  plot.kind = br_plot_instance_kind_2d;
  plot.graph_screen_rect = { GRAPH_LEFT_PAD, 50, (float)GetScreenWidth() - GRAPH_LEFT_PAD - 60, (float)GetScreenHeight() - 110 };
  plot.resolution = { (float)GetScreenWidth(), (float)GetScreenHeight() };
  plot.follow = false;
  plot.dd.line_shader = br->shaders.line;
  plot.dd.grid_shader = br->shaders.grid,
  plot.dd.zoom = Vector2 { 1.f, 1.f },
  plot.dd.offset = { 0, 0 };
  br_da_push_t(int, (br->plots), plot);

  ctx = glfwGetCurrentContext();
  ImGui::SetAllocatorFunctions(BR_IMGUI_MALLOC, BR_IMGUI_FREE, nullptr);
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();
#ifdef PLATFORM_WEB
  const char* glsl_version = "#version 100";
  io.IniFilename = nullptr;
#elif PLATFORM_DESKTOP
  const char* glsl_version = "#version 330";
#endif
  ImGui_ImplGlfw_InitForOpenGL(ctx, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImGuiStyle& s = ImGui::GetStyle();
  s.Colors[ImGuiCol_WindowBg].w = 0.f;
#ifndef RELEASE
#ifdef LINUX
  br_hotreload_start(&br->hot_state);
#endif
#endif
    ImGui::LoadIniSettingsFromMemory(DEFAULT_INI);
#ifndef PLATFORM_WEB
    ImGui::LoadIniSettingsFromDisk("imgui.ini");
#endif
}

extern "C" void br_gui_free_specifics(br_plotter_t* br) {
  (void)br;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void graph_draw_min(br_plotter_t* br, br_plot_instance_t* plot, float posx, float posy, float width, float height, float padding) {
  plot->resolution.x = (float)GetScreenWidth();
  plot->resolution.y = (float)GetScreenHeight();
  plot->graph_screen_rect.x = 50.f + posx + padding;
  plot->graph_screen_rect.y = posy + padding;
  plot->graph_screen_rect.width = width - 50.f - 2.f * padding;
  plot->graph_screen_rect.height = height - 30.f - 2.f * padding;

  //DrawRectangleRec(plot->graph_screen_rect, BLACK);
  br_plotter_update_variables(br);
  draw_grid_numbers(plot);
  smol_mesh_grid_draw(plot);
  points_groups_draw(br->groups, plot);
}

extern "C" void br_plotter_draw(br_plotter_t* gv) {
#ifndef RELEASE
#ifdef LINUX
  if (gv->hot_state.is_init_called == false && gv->hot_state.func_init != nullptr) {
    pthread_mutex_lock(&gv->hot_state.lock);
      if (gv->hot_state.func_init != nullptr) {
        gv->hot_state.func_init(gv);
        gv->hot_state.is_init_called = true;
      }
    pthread_mutex_unlock(&gv->hot_state.lock);
  }
#endif
#endif
  BeginDrawing();
  ImGui_ImplGlfw_NewFrame();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();
  ImGui::DockSpaceOverViewport();
  br_plot_instance_t* plot = &gv->plots.arr[0];
  if (ImGui::Begin("Plot") && false == ImGui::IsWindowHidden()) {
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    graph_draw_min(gv, plot, p.x, p.y, size.x, size.y, padding);
  }
  ImGui::End();
#ifndef RELEASE
#ifdef LINUX
  if (gv->hot_state.func_loop != nullptr) {
    pthread_mutex_lock(&gv->hot_state.lock);
      if (gv->hot_state.func_loop != nullptr) gv->hot_state.func_loop(gv);
    pthread_mutex_unlock(&gv->hot_state.lock);
  }
  ImGui::SetNextWindowBgAlpha(0.7f);
  ImGui::ShowDemoWindow();
#endif
#endif
  br::ui_settings(gv);
  br::ui_info(gv);
  if (screenshot_file_save == 1) {
    fs = br_file_saver_malloc("Save screenshot", std::getenv("HOME"));
    screenshot_file_save = 2;
  }
  if (screenshot_file_save == 2) {
    br_file_saver_state_t state = br_file_explorer(fs);
    switch (state) {
      case file_saver_state_exploring: break;
      case file_saver_state_accept: {
                                      br_str_t s = br_str_malloc(64);
                                      br_file_saver_get_path(fs, &s);
                                      br_plot_instance_screenshot_imgui(plot, gv->groups, br_str_move_to_c_str(&s));
                                    } // FALLTHROUGH
      case file_saver_state_cancle: {
                                      br_file_saver_free(fs);
                                      screenshot_file_save = 0;
                                    } break;
    }
  }

  br_plotter_frame_end(gv);
  ImGui::Render();
  int display_h = 0, display_w = 0;
  glfwGetFramebufferSize(ctx, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  EndDrawing();
#ifndef PLATFORM_WEB
  ClearBackground(BLACK);
#endif
}

extern "C" void br_plot_instance_screenshot(br_plot_instance_t*, points_groups_t, char const*) {
  screenshot_file_save = 1;
  return;
}

static void br_plot_instance_screenshot_imgui(br_plot_instance_t* plot, points_groups_t groups, char* path) {
  float left_pad = 80.f;
  float bottom_pad = 80.f;
  Vector2 is = {1280, 720};
  RenderTexture2D target = LoadRenderTexture((int)is.x, (int)is.y); // TODO: make this values user defined.
  plot->graph_screen_rect = {left_pad, 0.f, is.x - left_pad, is.y - bottom_pad};
  plot->resolution = {is.x, is.y};
  br_plot_instance_update_context(plot, GetMousePosition());
  br_plot_instance_update_shader_values(plot);
  BeginTextureMode(target);
    smol_mesh_grid_draw(plot);
    points_groups_draw(groups, plot);
    draw_grid_numbers(plot);
  EndTextureMode();
  Image img = LoadImageFromTexture(target.texture);
  ImageFlipVertical(&img);
  ExportImage(img, path);
  UnloadImage(img);
  UnloadRenderTexture(target);
  BR_FREE(path);
}

