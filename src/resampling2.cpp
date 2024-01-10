#include <algorithm>
#include "br_plot.h"
#include "br_help.h"
#include "stdint.h"
#include "math.h"
#include "string.h"
#include "assert.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "raylib/src/raymath.h"
#pragma GCC diagnostic pop

#define RESAMPLING_NODE_MAX_LEN 128
#define RESAMPLING_RAW_NODE_MAX_LEN (RESAMPLING_NODE_MAX_LEN * 8)

typedef enum {
  resampling2_kind_x,
  resampling2_kind_y,
  resampling2_kind_raw
} resampling2_node_kind_t;

struct resampling2_node_t {
  template<resampling2_node_kind_t kind>
  constexpr bool is_inside(Vector2 const* points, Rectangle rect) const {
    float minx, miny, maxx, maxy;
    if constexpr (kind == resampling2_kind_y) {
      miny = points[index_start].y;
      maxy = points[index_start + len - 1].y;
      minx = points[min_index].x;
      maxx = points[max_index].x;
      if (maxy < miny) std::swap(maxy, miny);
    } else {
      minx = points[index_start].x;
      maxx = points[index_start + len - 1].x;
      miny = points[min_index].y;
      maxy = points[max_index].y;
      if (maxx < minx) std::swap(maxx, minx);
    }
    return !((maxy < rect.y) || (miny > rect.y + rect.height) || (minx > rect.x + rect.width) || (maxx < rect.x));
  }

  template<resampling2_node_kind_t kind>
  constexpr Vector2 get_ratios(Vector2 const* points, float screen_width, float screen_height) const {
    Vector2 p1 = points[index_start], p2 = points[index_start + len - 1];
    float xr, yr;
    if constexpr (kind == resampling2_kind_x) xr = fabsf(p1.x - p2.x) / screen_width,  yr = (points[max_index].y - points[min_index].y) / screen_height;
    else                                      yr = fabsf(p1.y - p2.y) / screen_height, xr = (points[max_index].x - points[min_index].x) / screen_width;
    return {xr, yr};
  }

  uint32_t min_index = 0, max_index = 0;
  uint32_t index_start = 0, len = 0;
};

struct resampling2_raw_node_t {
  uint32_t index_start = 0, len = 0;
  uint32_t minx_index = 0, maxx_index = 0, miny_index = 0, maxy_index = 0;
};

typedef struct resampling2_nodes_s {
  resampling2_node_t* arr = NULL;
  struct resampling2_nodes_s* parent = NULL;
  uint32_t len = 0;
  uint32_t cap = 0;
  bool is_rising = true, is_falling = true;
} resampling2_nodes_t;

enum resampling2_axis : int32_t {
  AXIS_X,
  AXIS_Y
};

struct resampling2_all_roots{
  resampling2_node_kind_t kind;
  union {
    resampling2_nodes_t x;
    resampling2_nodes_t y;
    resampling2_raw_node_t raw;
  };
  constexpr resampling2_all_roots(resampling2_nodes_t nodes, resampling2_node_kind_t kind) : kind(kind), x(nodes) {}
  constexpr resampling2_all_roots(resampling2_raw_node_t raw) : kind(resampling2_kind_raw), raw(raw) {}
};

typedef struct resampling2_s {
  resampling2_all_roots* roots = nullptr;
  uint32_t roots_len = 0;
  uint32_t roots_cap = 0;

  resampling2_nodes_t temp_root_x = {}, temp_root_y = {};
  resampling2_raw_node_t temp_root_raw = {};
  bool temp_x_valid = true, temp_y_valid = true, temp_raw_valid = true;
} resampling2_t;

static uint32_t powers[32] = {0};
static uint32_t powers_base = 2;
static void __attribute__((constructor(101))) construct_powers(void) {
  powers[0] = 1;
  powers[1] = powers_base;
  for (int i = 2; i < 32; ++i) {
    powers[i] = powers[i - 1] * powers_base;
  }
}

static void resampling2_nodes_deinit(resampling2_nodes_t* nodes);
static void resampling2_nodes_empty(resampling2_nodes_t* nodes);
static void resampling2_push_root(resampling2_t* r, resampling2_all_roots root);
static resampling2_node_t* resampling2_get_last_node(resampling2_nodes_t* nodes);
template<resampling2_node_kind_t kind>
static void resampling2_nodes_push_point(resampling2_nodes_t* nodes, uint32_t index, Vector2 const* points, uint8_t depth);
template<resampling2_node_kind_t kind>
static bool resampling2_add_point(resampling2_nodes_t* nodes, points_group_t const* pg, uint32_t index);
template<resampling2_node_kind_t kind>
static ssize_t resampling2_get_first_inside(resampling2_nodes_t const* nodes, Vector2 const* points, Rectangle rect, uint32_t start_index);
template<resampling2_node_kind_t kind>
static void resampling2_draw(resampling2_nodes_t const* nodes, points_group_t const* pg, points_groups_draw_in_t *rdi);
static bool resampling2_add_point_raw(resampling2_raw_node_t* node, Vector2 const* points, uint32_t index);
static void resampling2_draw(resampling2_raw_node_t raw, points_group_t const* pg, points_groups_draw_in_t *rdi);
static void resampling2_draw(resampling2_all_roots r, points_group_t const* pg, points_groups_draw_in_t *rdi);

resampling2_t* resampling2_malloc(void) {
   resampling2_t* r = (resampling2_t*)BR_CALLOC(1, sizeof(resampling2_t));
   r->temp_x_valid = r->temp_y_valid = r->temp_raw_valid = true;
   return r;
}

void resampling2_empty(resampling2_t* res) {
  for (uint32_t i = 0; i < res->roots_len; ++i) {
    if (res->roots[i].kind != resampling2_kind_raw) {
      resampling2_nodes_empty(&res->roots[i].x);
    }
  }
  resampling2_nodes_deinit(&res->temp_root_x);
  resampling2_nodes_deinit(&res->temp_root_y);
  res->temp_root_x = {};
  res->temp_root_y = {};
  res->temp_root_raw = {};
  res->temp_raw_valid = res->temp_y_valid = res->temp_x_valid = true;
}

void resampling2_free(resampling2_t* r) {
  for (uint32_t i = 0; i < r->roots_len; ++i) {
    if (r->roots[i].kind != resampling2_kind_raw)
    resampling2_nodes_deinit(&r->roots[i].x);
  }
  resampling2_nodes_deinit(&r->temp_root_x);
  resampling2_nodes_deinit(&r->temp_root_y);
  BR_FREE(r->roots);
  BR_FREE(r);
}

extern "C" void resampling2_add_point(resampling2_t* r, const points_group_t *pg, uint32_t index) {
  bool was_valid_x = r->temp_x_valid, was_valid_y = r->temp_y_valid, was_valid_raw = r->temp_raw_valid;
  if (was_valid_x)   r->temp_x_valid   = resampling2_add_point<resampling2_kind_x>(&r->temp_root_x, pg, index);
  if (was_valid_y)   r->temp_y_valid   = resampling2_add_point<resampling2_kind_y>(&r->temp_root_y, pg, index);
  if (was_valid_raw) r->temp_raw_valid = resampling2_add_point_raw(&r->temp_root_raw, pg->points, index);
  if (r->temp_x_valid || r->temp_y_valid || r->temp_raw_valid) return;
  if (was_valid_x) {
    resampling2_push_root(r, resampling2_all_roots(r->temp_root_x, resampling2_kind_x));
    resampling2_nodes_deinit(&r->temp_root_y);
  } else if (was_valid_y) {
    resampling2_push_root(r, resampling2_all_roots(r->temp_root_y, resampling2_kind_y));
    resampling2_nodes_deinit(&r->temp_root_x);
  } else if (was_valid_raw) {
    resampling2_push_root(r, resampling2_all_roots(r->temp_root_raw) );
    resampling2_nodes_deinit(&r->temp_root_x);
    resampling2_nodes_deinit(&r->temp_root_y);
  } else assert(false);
  r->temp_root_x = {};
  r->temp_root_y = {};
  r->temp_root_raw = {};
  r->temp_raw_valid = r->temp_y_valid = r->temp_x_valid = true;
  resampling2_add_point(r, pg, index);
}

static void resampling2_nodes_deinit(resampling2_nodes_t* nodes) {
  if (nodes == NULL) return;
  resampling2_nodes_deinit(nodes->parent);
  BR_FREE(nodes->arr);
  BR_FREE(nodes->parent);
}

static void resampling2_nodes_empty(resampling2_nodes_t* nodes) {
  if (nodes == NULL) return;
  resampling2_nodes_empty(nodes->parent);
  nodes->len = 0;
}

static void resampling2_push_root(resampling2_t* r, resampling2_all_roots root) {
  if (r->roots_len == 0) {
    r->roots = (resampling2_all_roots*)BR_MALLOC(sizeof(resampling2_all_roots));
    r->roots_cap = 1;
  }
  if (r->roots_len == r->roots_cap) {
    uint32_t new_cap = r->roots_cap * 2;
    r->roots = (resampling2_all_roots*)BR_REALLOC(r->roots, new_cap * sizeof(resampling2_all_roots));
    r->roots_cap = new_cap;
  }
  r->roots[r->roots_len++] = root;
}

static resampling2_node_t* resampling2_get_last_node(resampling2_nodes_t* nodes) {
  if (nodes->len == 0) {
    nodes->arr = (resampling2_node_t*)BR_CALLOC(1, sizeof(resampling2_node_t));
    nodes->len = 1;
    nodes->cap = 1;
  }
  return &nodes->arr[nodes->len - 1];
}

template<resampling2_node_kind_t kind>
static void resampling2_nodes_push_point(resampling2_nodes_t* nodes, uint32_t index, Vector2 const* points, uint8_t depth) {
  resampling2_node_t* node = resampling2_get_last_node(nodes);
  unsigned int node_i = nodes->len - 1;
  if (node->len == 0) {
    node->min_index = index;
    node->max_index = index;
    node->len = 1;
    node->index_start = index;
  } else if (node->len < (RESAMPLING_NODE_MAX_LEN * powers[depth])) {
    if constexpr (kind == resampling2_kind_y) {
      if (points[node->min_index].x > points[index].x) node->min_index = index;
      if (points[node->max_index].x < points[index].x) node->max_index = index;
    } else {
      if (points[node->min_index].y > points[index].y) node->min_index = index;
      if (points[node->max_index].y < points[index].y) node->max_index = index;
    }
    ++node->len;
  } else if (node->len == (RESAMPLING_NODE_MAX_LEN * powers[depth])) {
    unsigned int new_cap = nodes->cap * 2;
    if (nodes->len == nodes->cap) {
      nodes->arr = (resampling2_node_t*)BR_REALLOC(nodes->arr, sizeof(resampling2_node_t) * (new_cap));
      for (unsigned int i = nodes->cap; i < new_cap; ++i) {
        nodes->arr[i] = {};
      }
      nodes->cap = new_cap;
    }
    ++nodes->len;
    node = &nodes->arr[node_i];
    if (nodes->parent == NULL) {
      nodes->parent = (resampling2_nodes_t*)BR_CALLOC(1, sizeof(resampling2_nodes_t));
      nodes->parent->arr = (resampling2_node_t*)BR_CALLOC(1, sizeof(resampling2_node_t));
      nodes->parent->len = 1;
      nodes->parent->cap = 1;
      memcpy(nodes->parent->arr, node, sizeof(resampling2_node_t));
      resampling2_nodes_push_point<kind>(nodes, index, points, depth);
    } else {
      resampling2_nodes_push_point<kind>(nodes, index, points, depth);
    }
    return;
  } else assert(false);
  if (nodes->parent != NULL) resampling2_nodes_push_point<kind>(nodes->parent, index, points, depth + 1);
}

template<resampling2_node_kind_t kind>
static bool resampling2_add_point(resampling2_nodes_t* nodes, const points_group_t *pg, uint32_t index) {
  Vector2 p = pg->points[index];
  if (nodes->len == 0) nodes->is_rising = nodes->is_falling = true;
  if (index == 0) {
    nodes->is_rising = true;
    nodes->is_falling = true;
    resampling2_nodes_push_point<kind>(nodes, index, pg->points, 0);
    return true;
  } 
  float new_v, old_v;
  if constexpr (kind == resampling2_kind_y) new_v = p.y, old_v = pg->points[index - 1].y;
  else                                      new_v = p.x, old_v = pg->points[index - 1].x;
  if (new_v > old_v) {
    nodes->is_falling = false;
    if (nodes->is_rising) {
      resampling2_nodes_push_point<kind>(nodes, index, pg->points, 0);
    } else {
      return false;
    }
  } else if (new_v < old_v) {
    nodes->is_rising = false;
    if (nodes->is_falling) {
      resampling2_nodes_push_point<kind>(nodes, index, pg->points, 0);
    } else {
      return false;
    }
  } else if (new_v == old_v) {
      resampling2_nodes_push_point<kind>(nodes, index, pg->points, 0);
  }
  // point must be nan, so let's just ignore it...
  return true;
}

template<resampling2_node_kind_t kind>
static ssize_t resampling2_get_first_inside(resampling2_nodes_t const* nodes, Vector2 const * const points, Rectangle rect, uint32_t start_index) {
  while (start_index % powers_base != 0) {
    if (start_index == nodes->len) return -1;
    if (nodes->arr[start_index].is_inside<kind>(points, rect)) return start_index;
    ++start_index;
  }
  if (nodes->parent != NULL) {
    ssize_t new_i = resampling2_get_first_inside<kind>(nodes->parent, points, rect, start_index / powers_base);
    if (new_i < 0) return -1;
    start_index = ((uint32_t)new_i) * powers_base;
  }
  for (; start_index < nodes->len; ++start_index) {
    if (nodes->arr[start_index].is_inside<kind>(points, rect)) {
      return (ssize_t)start_index;
    }
  }
  return -1;
}

float something = 0.02f;
float something2 = 0.001f;
float stride_after = 0.06f;
int max_stride = 10;
int raw_c = 0;
int not_raw_c = 0;
template<resampling2_node_kind_t kind>
static void resampling2_draw(resampling2_nodes_t const* nodes, points_group_t const* pg, points_groups_draw_in_t *rdi) {
  ssize_t j = 0;
  j = resampling2_get_first_inside<kind>(nodes, pg->points, rdi->rect, (uint32_t)j);
  while (j != -1) {
    resampling2_node_t n = nodes->arr[j];
    Vector2 const* ps = pg->points;
    Vector2 first = ps[n.index_start], last = ps[n.index_start + n.len - 1];
    Vector2 ratios = n.get_ratios<kind>(ps, rdi->rect.width, rdi->rect.height);
    float ratio_min = fminf(ratios.x, ratios.y);
    int depth = 0;
    if (ratio_min > something) {
      if (ratio_min > stride_after) {
        smol_mesh_gen_line_strip(rdi->line_mesh, &ps[n.index_start], n.len, pg->color);
      } else {
        int cur_stride = 1 + (int)((stride_after - ratio_min) / (stride_after - something) * (float)max_stride);
        smol_mesh_gen_line_strip_stride(rdi->line_mesh, &ps[n.index_start], n.len, pg->color, cur_stride);
      }
      raw_c++;
    } else {
      resampling2_nodes_t const* curn = nodes;
      ssize_t curj = j;
      while (curn->parent != NULL && curj % powers_base == 0 && (ratio_min < something2)) {
        curj /= powers_base;
        curn = curn->parent;
        n = curn->arr[curj];
        first = ps[n.index_start], last = ps[n.index_start + n.len - 1];
        ratios = n.get_ratios<kind>(ps, rdi->rect.width, rdi->rect.height);
        ratio_min = fminf(ratios.x, ratios.y);
        ++depth;
      }
      bool swp = false;
      if constexpr (kind == resampling2_kind_x) swp = (first.x < last.x) == (ps[n.min_index].x < ps[n.max_index].x);
      else                                      swp = (first.y < last.y) == (ps[n.min_index].y < ps[n.max_index].y);
      Vector2 pss[] = { 
        first,
        swp ? ps[n.min_index] : ps[n.max_index],
        swp ? ps[n.max_index] : ps[n.min_index],
        last
      };
      smol_mesh_gen_line_strip(rdi->line_mesh, pss, 4, pg->color);
      not_raw_c++;
    }
    j += (ssize_t)powers[depth];
    if (j < nodes->len) {
      uint32_t next_index = nodes->arr[j].index_start;
      smol_mesh_gen_line(rdi->line_mesh, last, ps[next_index], pg->color);
    }
    j = resampling2_get_first_inside<kind>(nodes, pg->points, rdi->rect, (uint32_t)j);
  }
}

static bool resampling2_add_point_raw(resampling2_raw_node_t* node, Vector2 const* points, uint32_t index) {
  if (node->len == RESAMPLING_NODE_MAX_LEN) return false;
  if (node->len == 0) {
    node->minx_index = node->maxx_index = node->miny_index = node->maxy_index = node->index_start = index;
  } else {
    if (points[node->minx_index].x > points[index].x) node->minx_index = index;
    if (points[node->miny_index].y > points[index].y) node->miny_index = index;
    if (points[node->maxx_index].x < points[index].x) node->maxx_index = index;
    if (points[node->maxy_index].y < points[index].y) node->maxy_index = index;
  }
  ++node->len;
  return true;
}

static void resampling2_draw(resampling2_raw_node_t raw, points_group_t const* pg, points_groups_draw_in_t *rdi) {
  Vector2 const* ps = pg->points;
  bool is_inside = !((ps[raw.maxy_index].y < rdi->rect.y) || (ps[raw.miny_index].y > rdi->rect.y + rdi->rect.height) ||
                     (ps[raw.maxx_index].x < rdi->rect.x) || (ps[raw.maxx_index].x < rdi->rect.x + rdi->rect.width));
  if (!is_inside) return;

  smol_mesh_gen_line_strip(rdi->line_mesh, &pg->points[raw.index_start], raw.len, pg->color);
}

static void resampling2_draw(resampling2_all_roots r, points_group_t const* pg, points_groups_draw_in_t *rdi) {
  if (r.kind == resampling2_kind_raw) {
    resampling2_draw(r.raw, pg, rdi);
  } else if (r.kind == resampling2_kind_x) {
    resampling2_draw<resampling2_kind_x>(&r.x, pg, rdi);
  } else if (r.kind == resampling2_kind_y) {
    resampling2_draw<resampling2_kind_y>(&r.y, pg, rdi);
  }
}

void resampling2_draw(resampling2_t const* res, points_group_t const *pg, points_groups_draw_in_t *rdi) {
  for (uint32_t i = 0; i < res->roots_len; ++i) {
    resampling2_draw(res->roots[i], pg, rdi);
  }
  if (res->temp_x_valid) resampling2_draw<resampling2_kind_x>(&res->temp_root_x, pg, rdi);
  else if (res->temp_y_valid) resampling2_draw<resampling2_kind_y>(&res->temp_root_y, pg, rdi);
  else resampling2_draw(res->temp_root_raw, pg, rdi);
}


static void resampling2_node_debug_print(FILE* file, resampling2_node_t* r, int depth) {
  if (r == NULL) return;
  for (int i = 0; i < depth; ++i) {
    fprintf(file, "  ");
  }
  fprintf(file, "Node len: %u\n", r->len);
}

static void resampling2_nodes_debug_print(FILE* file, resampling2_nodes_t* r, int depth) {
  if (r == NULL) return;
  for (int i = 0; i < depth; ++i) {
    fprintf(file, "  ");
  }
  fprintf(file, "len: %u, cap: %u, rising: %d, falling: %d\n", r->len, r->cap, r->is_rising, r->is_falling);
  for (unsigned int i = 0; i < r->len; ++i) {
    resampling2_node_debug_print(file, &r->arr[i], depth + 1);
  }
  resampling2_nodes_debug_print(file, r->parent, depth + 1);
}

static void resampling2_raw_node_debug_print(FILE* file, resampling2_raw_node_t* node, int depth) {
  for (int i = 0; i < depth; ++i) {
    fprintf(file, "  ");
  }
  fprintf(file, "Raw node, from: %u, len: %u\n", node->index_start, node->len);
}

static void resampling2_all_nodes_debug_print(FILE* file, resampling2_all_roots* r) {
  const char* str;
  if (r->kind == resampling2_kind_x) str = "x";
  if (r->kind == resampling2_kind_y) str = "y";
  if (r->kind == resampling2_kind_raw) str = "raw";
  fprintf(file, "Nodes %s\n", str);
}

static void resampling2_debug_print(FILE* file, resampling2_t* r) {
  fprintf(file, "\nRoots len: %u, Roots cap: %u, validxyr = %d,%d,%d\n", r->roots_len, r->roots_cap, r->temp_x_valid, r->temp_y_valid, r->temp_raw_valid);
  for (uint32_t i = 0; i < r->roots_len; ++i) {
    resampling2_all_nodes_debug_print(file, &r->roots[i]);
  }
  resampling2_nodes_debug_print(file, &r->temp_root_x, 1);
  resampling2_nodes_debug_print(file, &r->temp_root_y, 1);
  resampling2_raw_node_debug_print(file, &r->temp_root_raw, 1);
  return;
}

#define PRINT_ALLOCS(prefix) \
  printf("\n%s ALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", prefix, \
      context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);

extern "C" {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "src/misc/tests.h"
TEST_CASE(resampling) {
  Vector2 points[] = { {0, 1}, {1, 2}, {2, 4}, {3, 2} };
  points_group_t pg = {
    .cap = 4,
    .len = 4,
    .points = points,
    .resampling = NULL
  };
  resampling2_t* r = resampling2_malloc();
  for (int i = 0; i < 2*1024; ++i) resampling2_add_point(r, &pg, 3);
  resampling2_debug_print(stdout, r);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  resampling2_add_point(r, &pg, 3);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  for (int i = 0; i < 64*1024; ++i) resampling2_add_point(r, &pg, 3);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  resampling2_free(r);
}

#define N 2048
TEST_CASE(resampling2) {
  Vector2 points[N];
  for (int i = 0; i < N; ++i) {
    points[i].x = sinf(3.14f / 4.f * (float)i);
    points[i].y = cosf(3.14f / 4.f * (float)i);
  }
  points_group_t pg = {
    .cap = N,
    .len = N,
    .points = points,
    .resampling = NULL
  };
  resampling2_t* r = resampling2_malloc();
  for (int i = 0; i < N; ++i) resampling2_add_point(r, &pg, (uint32_t)i);
  resampling2_debug_print(stdout, r);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  resampling2_add_point(r, &pg, 3);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  for (int i = 0; i < 64*1024; ++i) resampling2_add_point(r, &pg, 3);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  for (int i = 0; i < 1024*1024; ++i) resampling2_add_point(r, &pg, 3);
  printf("\nALLOCATIONS: %lu ( %luKB ) | %lu (%luKB)\n", context.alloc_count, context.alloc_size >> 10, context.alloc_total_count, context.alloc_total_size >> 10);
  resampling2_free(r);
}
#pragma GCC diagnostic pop
} // extern "C"
