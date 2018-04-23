#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

#include "SDL.h"
#include "SDL_image.h"
#include "font8x8_basic.h"

typedef unsigned char byte;

#define DELETED 0x1
#define BLOCK 0x2
#define BEAST 0x4
#define PLAYER 0x8
#define STONE 0x10
#define TURRET 0x20
#define POWER 0x40
#define SUPER 0x80

#define WATER 0x1

typedef struct {
  byte flags;
  byte health;
  int x;
  int y;
} Entity;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} Viewport;

typedef struct {
  byte flags;
  double x;
  double y;
  double dx;
  double dy;
} Bullet;

typedef struct {
  SDL_Texture* tex;
  int x;
  int y;
  int w;
  int h;
} Image;

// grid functions
bool in_bounds(int x, int y);
int find_avail_pos(Entity* grid[], byte grid_flags[]);
void move(Entity* ent, Entity* grid[], int x, int y);
void set_pos(Entity* ent, Entity* grid[], int pos);
void set_xy(Entity* ent, Entity* grid[], int x, int y);
void remove_from_grid(Entity* ent, Entity* grid[]);
int to_x(int ix);
int to_y(int ix);
int to_pos(int x, int y);
bool is_in_grid(int x, int y);

// game-specific functions
void play_level(SDL_Window* window, SDL_Renderer* renderer);
void load(Entity* grid[], byte grid_flags[], Entity blocks[], Entity power_stones[], Entity beasts[], Entity turrets[], Bullet bullets[]);
void gen_water(byte grid_flags[], short top_left, short top_right, short bottom_left, short bottom_right, int x, int y, int w);
void remove_sm_islands(byte grid_flags[]);
void remove_sm_lakes(byte grid_flags[]);
void on_mousemove(SDL_Event* evt, Entity* grid[], Entity turrets[], Entity power_stones[]);
void on_mousedown(SDL_Event* evt, Entity* grid[], Entity turrets[], Entity power_stones[]);
void on_keydown(SDL_Event* evt, Entity* grid[], bool* is_gameover, bool* is_paused, SDL_Window* window);
void on_scroll(SDL_Event* evt);
void update(double dt, unsigned int curr_time, Entity* grid[], Entity turrets[], Entity beasts[], Bullet bullets[]);
void render(SDL_Renderer* renderer, Image* ui_bar_img, SDL_Texture* sprites, byte grid_flags[], Entity blocks[], Entity power_stones[], Entity turrets[], Entity beasts[], Bullet bullets[]);

bool is_next_to_wall(Entity* beast, Entity* grid[]);
void beast_explode(Entity* beast, Entity* grid[]);
Entity* closest_entity(int x, int y, Entity entities[], int num_entities);
void del_entity(Entity* ent, Entity* grid[]);
void update_powered_turrets(Entity* grid[], Entity power_stones[]);
void set_powered(Entity* grid[], int x, int y);
int get_move_pos(Entity* beast, Entity turrets[], Entity* grid[]);

// generic functions
void toggle_fullscreen(SDL_Window *win);
double calc_dist(int x1, int y1, int x2, int y2);
int clamp(int val, int min, int max);
int render_text(SDL_Renderer* renderer, char str[], int offset_x, int offset_y, int size);
Image load_img(SDL_Renderer* renderer, char* path);
void render_img(SDL_Renderer* renderer, Image* img);
void center_img(Image* img, Viewport* viewport);
void render_sprite(SDL_Renderer* renderer, SDL_Texture* sprites, int src_x, int src_y, int dest_x, int dest_y);
void render_corner(SDL_Renderer* renderer, SDL_Texture* sprites, int src_x, int src_y, int dest_x, int dest_y);
bool is_mouseover(Image* img, int x, int y);
void error(char* activity);

// game globals
Viewport vp = {};

int num_collected_blocks;
int block_ratio = 2; // you have to collect 2 rocks to build 1 wall
int num_blocks_per_road = 3;
int num_blocks_per_turret = 25; // collect 5 rocks to build 1 turret
int num_blocks_per_refurb = 15; // discount if you "refurbish" an existing block to build a turret
int num_blocks_per_bridge = 50;
int attack_dist = 30; // how close a beast has to be before he moves towards you
byte beast_health = 3;

int block_w = 40;
int block_h = 40;
int bullet_w = 4;
int bullet_h = 4;
double bullet_speed = 300.0; // in px/sec
int block_density_pct = 2;

int num_blocks_w = 128; // 2^7
int num_blocks_h = 128; // 2^7
int grid_len;

unsigned int last_move_time = 0;
int beast_move_interval = 500; // ms between beast moves
unsigned int last_fire_time = 0;
int turret_fire_interval = 2000; // ms between turret firing
int mine_interval = 10000; // ms between mine generating metal

int max_beasts = 10;
int max_turrets = 50;
int max_bullets = 100;
int max_blocks;
int max_power_stones = 10;

// top level (title screen)
int main(int num_args, char* args[]) {
  srand(time(NULL));
  
  // SDL setup
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    error("initializing SDL");

  SDL_Window* window;
  window = SDL_CreateWindow("Future Fortress", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, num_blocks_w * block_w, num_blocks_h * block_h, SDL_WINDOW_RESIZABLE);
  if (!window)
    error("creating window");
  
  // toggle_fullscreen(window);
  SDL_GetWindowSize(window, &vp.w, &vp.h);

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  SDL_Cursor* arrow_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  SDL_Cursor* hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

  Image title_img = load_img(renderer, "images/title.png");
  title_img.y = 50;
  Image start_game_img = load_img(renderer, "images/start-game.png");
  start_game_img.y = 500;
  Image start_game_hover_img = load_img(renderer, "images/start-game-hover.png");
  start_game_hover_img.y = 500;
  Image hints_img = load_img(renderer, "images/hints.png");
  hints_img.y = 500 + start_game_img.h + 50;
  
  center_img(&title_img, &vp);
  center_img(&start_game_img, &vp);
  center_img(&start_game_hover_img, &vp);
  center_img(&hints_img, &vp);

  SDL_Event evt;
  bool exit_game = false;
  while (!exit_game) {
    while (SDL_PollEvent(&evt)) {
      if (evt.type == SDL_QUIT || (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE)) {
        exit_game = true;
      }
      else if (evt.type == SDL_MOUSEBUTTONDOWN && is_mouseover(&start_game_img, evt.button.x, evt.button.y)) {
        SDL_SetCursor(arrow_cursor);
        play_level(window, renderer);
      }
    }

    // set BG color
    if (SDL_SetRenderDrawColor(renderer, 44, 34, 30, 255) < 0)
      error("setting bg color");
    if (SDL_RenderClear(renderer) < 0)
      error("clearing renderer");
    
    int mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    
    render_img(renderer, &title_img);
    if (is_mouseover(&start_game_img, mouse_x, mouse_y)) {
      render_img(renderer, &start_game_hover_img);
      SDL_SetCursor(hand_cursor);
    }
    else {
      render_img(renderer, &start_game_img);
      SDL_SetCursor(arrow_cursor);
    }
    render_img(renderer, &hints_img);
    
    SDL_RenderPresent(renderer);
    SDL_Delay(10);
  }

  // if (SDL_SetWindowFullscreen(window, 0) < 0)
  //   error("exiting fullscreen");

  SDL_FreeCursor(arrow_cursor);
  SDL_FreeCursor(hand_cursor);

  SDL_DestroyTexture(title_img.tex);
  SDL_DestroyTexture(start_game_img.tex);
  SDL_DestroyTexture(start_game_hover_img.tex);
  SDL_DestroyTexture(hints_img.tex);
  
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

void play_level(SDL_Window* window, SDL_Renderer* renderer) {
  // reset global variables
  num_collected_blocks = 250;
  grid_len = num_blocks_w * num_blocks_h;
  max_blocks = grid_len * block_density_pct * 3 / 100; // x3 b/c default is 20% density, but we need up to 60% due to mines
  
  // load game
  Entity* grid[grid_len];
  byte grid_flags[grid_len];

  Entity blocks[max_blocks];
  Entity power_stones[max_power_stones];
  Entity beasts[max_beasts];
  Entity turrets[max_turrets];
  
  Bullet bullets[max_bullets];

  load(grid, grid_flags, blocks, power_stones, beasts, turrets, bullets);

  Image ui_bar_img = load_img(renderer, "images/ui-bar.png");
  SDL_Texture* sprites = IMG_LoadTexture(renderer, "images/spritesheet.png");

  // game loop (incl. events, update & draw)
  bool is_gameover = false;
  bool is_paused = false;
  unsigned int last_loop_time = SDL_GetTicks();
  while (!is_gameover) {
    SDL_Event evt;

    // handle pause state
    if (is_paused) {
      while (SDL_PollEvent(&evt))
        if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_SPACE)
          is_paused = false;

      if (is_paused) {
        SDL_Delay(10);
        continue;
      }
      else {
        // reset last_loop_time when coming out of a pause state
        // otherwise the game will react as if a ton of time has gone by
        last_loop_time = SDL_GetTicks();
      }
    }

    // manage delta time
    unsigned int curr_time = SDL_GetTicks();
    double dt = (curr_time - last_loop_time) / 1000.0; // dt should always be in seconds
    last_loop_time = curr_time;

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    bool is_spacebar_pressed = state[SDL_SCANCODE_SPACE]; // TODO: change cursor to hand & use it to scroll
    
    // handle events
    while (SDL_PollEvent(&evt)) {
      switch(evt.type) {
        case SDL_QUIT:
          is_gameover = true;
          break;
        case SDL_WINDOWEVENT:
          if (evt.window.event == SDL_WINDOWEVENT_RESIZED)
            SDL_GetWindowSize(window, &vp.w, &vp.h);
          break;
        case SDL_MOUSEMOTION:
          on_mousemove(&evt, grid, turrets, power_stones);
          break;
        case SDL_MOUSEBUTTONDOWN:
          on_mousedown(&evt, grid, turrets, power_stones);
          break;
        case SDL_KEYDOWN:
          on_keydown(&evt, grid, &is_gameover, &is_paused, window);
          break;
        case SDL_MOUSEWHEEL:
          on_scroll(&evt);
          break;
      }
    }

    update(dt, curr_time, grid, turrets, beasts, bullets);
    render(renderer, &ui_bar_img, sprites, grid_flags, blocks, power_stones, turrets, beasts, bullets);

    SDL_Delay(10);
  }

  SDL_DestroyTexture(sprites);
}

void load(Entity* grid[], byte grid_flags[], Entity blocks[], Entity power_stones[], Entity beasts[], Entity turrets[], Bullet bullets[]) {
  // have to manually init b/c C doesn't allow initializing VLAs w/ {0}
  for (int i = 0; i < grid_len; ++i) {
    grid[i] = NULL;
    grid_flags[i] = 0;
  }

  gen_water(grid_flags, 0, 0, 0, 0, 0,0, num_blocks_w);
  remove_sm_islands(grid_flags);
  remove_sm_lakes(grid_flags);

  // add power stones to the playing field
  for (int i = 0; i < max_power_stones; ++i) {
    int pos = find_avail_pos(grid, grid_flags);
    power_stones[i].flags = (BLOCK | STONE);
    power_stones[i].x = to_x(pos);
    power_stones[i].y = to_y(pos);
    grid[pos] = &power_stones[i];
  }

  for (int i = 0; i < max_blocks; ++i) {
    if (i < grid_len * block_density_pct / 100) {
      int pos = find_avail_pos(grid, grid_flags);
      blocks[i].flags = BLOCK;
      blocks[i].x = to_x(pos);
      blocks[i].y = to_y(pos);
      grid[pos] = &blocks[i];
    }
    else {
      blocks[i].flags = BLOCK | DELETED;
    }
  }

  for (int i = 0; i < max_beasts; ++i) {
    beasts[i].flags = BEAST;
    if (i == 0)
      beasts[i].flags |= SUPER;

    int pos = find_avail_pos(grid, grid_flags);
    beasts[i].x = to_x(pos);
    beasts[i].y = to_y(pos);
    beasts[i].health = beast_health;
    grid[pos] = &beasts[i];
  }

  // precreate all turrets "blocks" as deleted
  for (int i = 0; i < max_turrets; ++i)
    turrets[i].flags = BLOCK | TURRET | DELETED;

  for (int i = 0; i < max_bullets; ++i)
    bullets[i].flags = DELETED;
}

void gen_water(byte grid_flags[], short top_left, short top_right, short bottom_left, short bottom_right, int x, int y, int w) {
  short water_level = 0;
  short avg = (top_left + top_right + bottom_left + bottom_right) / 4;
  short deviation = rand() % USHRT_MAX - SHRT_MAX; // generate a random signed short
  short center = clamp(avg + deviation, SHRT_MIN, SHRT_MAX);
  
  // for now, set center val to top center, bottom center, right center, left center
  if (center < water_level) {
    grid_flags[to_pos(x+w/2, y)] |= WATER; // top center
    grid_flags[to_pos(x+w/2, y+w-1)] |= WATER; // bottom center
    grid_flags[to_pos(x, y+w/2)] |= WATER; // left center
    grid_flags[to_pos(x+w-1, y+w/2)] |= WATER; // right center
  }
  
  // recurse to calc nested squares
  if (w >= 2) {
    w = w / 2;
    gen_water(grid_flags, center, center, center, center, x,y, w);
    gen_water(grid_flags, center, center, center, center, x+w,y, w);
    gen_water(grid_flags, center, center, center, center, x,y+w, w);
    gen_water(grid_flags, center, center, center, center, x+w,y+w, w);
  }
}

void remove_sm_islands(byte grid_flags[]) {
  for (int x = 0; x < num_blocks_w; ++x) {
    for (int y = 0; y < num_blocks_h; ++y) {
      if (grid_flags[to_pos(x, y)] & WATER)
        continue;

      bool has_adj_land = false;
      if (x > 0 && !(grid_flags[to_pos(x - 1, y)] & WATER))
        has_adj_land = true;
      if (y > 0 && !(grid_flags[to_pos(x, y - 1)] & WATER))
        has_adj_land = true;
      if (x < (num_blocks_w - 1) && !(grid_flags[to_pos(x + 1, y)] & WATER))
        has_adj_land = true;
      if (y < (num_blocks_h - 1) && !(grid_flags[to_pos(x, y + 1)] & WATER))
        has_adj_land = true;

      // if it's a tiny 1-square island, flood it w/ water
      if (!has_adj_land)
        grid_flags[to_pos(x, y)] |= WATER;
    }
  }
}

void remove_sm_lakes(byte grid_flags[]) {
  for (int x = 0; x < num_blocks_w; ++x) {
    for (int y = 0; y < num_blocks_h; ++y) {
      if (!(grid_flags[to_pos(x, y)] & WATER))
        continue;

      bool has_adj_water = false;
      if (x > 0 && grid_flags[to_pos(x - 1, y)] & WATER)
        has_adj_water = true;
      if (y > 0 && grid_flags[to_pos(x, y - 1)] & WATER)
        has_adj_water = true;
      if (x < (num_blocks_w - 1) && grid_flags[to_pos(x + 1, y)] & WATER)
        has_adj_water = true;
      if (y < (num_blocks_h - 1) && grid_flags[to_pos(x, y + 1)] & WATER)
        has_adj_water = true;

      // if it's a tiny 1-square lake, remove the water
      if (!has_adj_water)
        grid_flags[to_pos(x, y)] &= ~(WATER);
    }
  }
}

// TODO: fix major DRY violation btwn mousemove & mousedown
void on_mousemove(SDL_Event* evt, Entity* grid[], Entity turrets[], Entity power_stones[]) {
  if (!(evt->motion.state & SDL_BUTTON_LMASK))
    return;

  int x = (evt->button.x + vp.x) / block_w;
  int y = (evt->button.y + vp.y) / block_h;
  int pos = to_pos(x, y);

  bool is_refurb = grid[pos] && grid[pos]->flags == BLOCK;
  int num_required_blocks = is_refurb ? num_blocks_per_refurb : num_blocks_per_turret;
  if ((grid[pos] && !is_refurb) || num_collected_blocks < num_required_blocks)
    return;

  for (int i = 0; i < max_turrets; ++i) {
    if (turrets[i].flags & DELETED) {
      if (is_refurb)
        del_entity(grid[pos], grid);

      num_collected_blocks -= num_required_blocks;
      turrets[i].flags &= (~DELETED); // clear deleted bit
      set_xy(&turrets[i], grid, x, y);
      update_powered_turrets(grid, power_stones);
      break;
    }
  }
  // TODO: determine if max_turrets has been reached & alert the player
}

void on_mousedown(SDL_Event* evt, Entity* grid[], Entity turrets[], Entity power_stones[]) {
  int x = (evt->button.x + vp.x) / block_w;
  int y = (evt->button.y + vp.y) / block_h;
  int pos = to_pos(x, y);

  bool is_refurb = grid[pos] && grid[pos]->flags == BLOCK;
  int num_required_blocks = is_refurb ? num_blocks_per_refurb : num_blocks_per_turret;
  if ((grid[pos] && !is_refurb) || num_collected_blocks < num_required_blocks)
    return;

  for (int i = 0; i < max_turrets; ++i) {
    if (turrets[i].flags & DELETED) {
      if (is_refurb)
        del_entity(grid[pos], grid);
      
      num_collected_blocks -= num_required_blocks;
      turrets[i].flags &= (~DELETED); // clear deleted bit
      set_xy(&turrets[i], grid, x, y);
      update_powered_turrets(grid, power_stones);
      break;
    }
  }
  // TODO: determine if max_turrets has been reached & alert the player?
}

void on_keydown(SDL_Event* evt, Entity* grid[], bool* is_gameover, bool* is_paused, SDL_Window* window) {
  switch (evt->key.keysym.sym) {
    case SDLK_ESCAPE:
      *is_gameover = true;
      break;
    case SDLK_f:
      toggle_fullscreen(window);
      break;
    case SDLK_SPACE:
      *is_paused = !*is_paused;
      break;
  }
}

void on_scroll(SDL_Event* evt) {
  int dx = evt->wheel.x * 8;
  int dy = evt->wheel.y * 8;
  if (evt->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
    dy = -dy;

  vp.x = clamp(vp.x + dx, 0, num_blocks_w * block_w);
  vp.y = clamp(vp.y + dy, 0, num_blocks_h * block_h);
}

void update(double dt, unsigned int curr_time, Entity* grid[], Entity turrets[], Entity beasts[], Bullet bullets[]) {
  // turret firing
  if (curr_time - last_fire_time >= mine_interval) {
    for (int i = 0; i < max_turrets; ++i) {
      Entity* turret = &turrets[i];
      if (!(turret->flags & DELETED))
        num_collected_blocks++;
    }
    last_fire_time = curr_time;
  }

  // if (curr_time - last_fire_time >= turret_fire_interval) {
  //   for (int i = 0; i < max_turrets; ++i) {
  //     Entity* turret = &turrets[i];
  //     if (turret->flags & DELETED)
  //       continue;

  //     // only turrets that are part of an enclosure can fire
  //     int pos = to_pos(turret->x, turret->y);
  //     if (!(turret->flags & POWER))
  //       continue;

  //     Entity* beast = closest_entity(turret->x, turret->y, beasts, max_beasts);
  //     if (beast) {
  //       double dist = calc_dist(beast->x, beast->y, turret->x, turret->y);
  //       // dividing by the distance gives us a normalized 1-unit vector
  //       double dx = (beast->x - turret->x) / dist;
  //       double dy = (beast->y - turret->y) / dist;
  //       for (int j = 0; j < max_bullets; ++j) {
  //         Bullet* b = &bullets[j];
  //         if (b->flags & DELETED) {
  //           b->flags &= (~DELETED); // clear the DELETED bit

  //           // super turrets make super bullets
  //           if (turret->flags & SUPER)
  //             b->flags |= SUPER;
            
  //           // start in top/left corner
  //           int start_x = turret->x * block_w;
  //           int start_y = turret->y * block_h;
  //           if (dx > 0)
  //             start_x += block_w;
  //           else
  //             start_x -= 1; // so it's not on top of itself
  //           if (dy > 0)
  //             start_y += block_h;
  //           else
  //             start_y -= 1; // so it's not on top of itself

  //           b->x = start_x;
  //           b->y = start_y;
  //           b->dx = dx;
  //           b->dy = dy;
  //           break;
  //         }
  //       }
  //     }
  //     // TODO: determine when max_bullets is exceeded & notify player?
  //   }
  //   last_fire_time = curr_time;
  // }

  // beast moving
  if (curr_time - last_move_time >= beast_move_interval) {
    for (int i = 0; i < max_beasts; ++i) {
      if (beasts[i].flags & DELETED)
        continue;

      if (is_next_to_wall(&beasts[i], grid)) {
        if (beasts[i].flags & SUPER || rand() % 100 >= 98) {
          beast_explode(&beasts[i], grid);
          continue;
        }
      }

      int dest_pos = get_move_pos(&beasts[i], turrets, grid);

      // if the beast is surrounded by blocks & has nowhere to move, it blows up
      if (dest_pos == -1)
        beast_explode(&beasts[i], grid);
      else
        move(&beasts[i], grid, to_x(dest_pos), to_y(dest_pos));
    }
    last_move_time = curr_time;
  }

  // update bullet positions; handle bullet collisions
  for (int i = 0; i < max_bullets; ++i) {
    if (bullets[i].flags & DELETED)
      continue;

    bullets[i].x += bullets[i].dx * bullet_speed * dt;
    bullets[i].y += bullets[i].dy * bullet_speed * dt;
    // delete bullets that have gone out of the game
    if ((bullets[i].x < 0 || bullets[i].x > num_blocks_w * block_w) ||
      bullets[i].y < 0 || bullets[i].y > num_blocks_h * block_h) {
        bullets[i].flags |= DELETED; // set deleted bit on
        continue;
    }

    int grid_x = bullets[i].x / block_w;
    int grid_y = bullets[i].y / block_h;
    if (is_in_grid(grid_x, grid_y)) {
      int pos = to_pos(grid_x, grid_y);
      Entity* ent = grid[pos];
      if (ent && ent->flags & BLOCK) {
        bullets[i].flags |= DELETED;
        continue;
      }
      else if (ent && ent->flags & BEAST) {
        // if it's a super Bullet or NOT a super-beast, "kill the beast!"
        if (bullets[i].flags & SUPER || !(ent->flags & SUPER)) {
          ent->health--;
          if (!ent->health)
            del_entity(ent, grid);
        }
        bullets[i].flags |= DELETED;
      }
    }
  }
}

void render(SDL_Renderer* renderer, Image* ui_bar_img, SDL_Texture* sprites, byte grid_flags[], Entity blocks[], Entity power_stones[], Entity turrets[], Entity beasts[], Bullet bullets[]) {
  // set BG color
  if (SDL_SetRenderDrawColor(renderer, 44, 34, 30, 255) < 0)
    error("setting bg color");
  if (SDL_RenderClear(renderer) < 0)
    error("clearing renderer");

  if (SDL_SetRenderDrawColor(renderer, 145, 103, 47, 255) < 0)
    error("setting land color");

  for (int i = 0; i < grid_len; ++i) {
    int x = to_x(i);
    int y = to_y(i);

    if (grid_flags[i] & WATER) {
      for (int corner_x = 0; corner_x <= 1; ++corner_x) {
        for (int corner_y = 0; corner_y <= 1; ++corner_y) {
          int adj_x = corner_x ? x + 1 : x - 1;
          int adj_y = corner_y ? y + 1 : y - 1;

          // treat edges as water
          if (adj_x < 0 || adj_x >= num_blocks_w || adj_y < 0 || adj_y >= num_blocks_h)
            continue;

          // if there is adjacent land in both directions & diagonally, round the (interior/acute) corner
          if (!(grid_flags[to_pos(adj_x, y)] & WATER) && !(grid_flags[to_pos(x, adj_y)] & WATER) && !(grid_flags[to_pos(adj_x, adj_y)] & WATER))
            render_corner(renderer, sprites, 8 + corner_x, 0 + corner_y, x * 2 + corner_x, y * 2 + corner_y);
        }
      }
    }
    else {
      // draw each corner, rounded if necessary
      for (int corner_x = 0; corner_x <= 1; ++corner_x) {
        for (int corner_y = 0; corner_y <= 1; ++corner_y) {
          int adj_x = corner_x ? x + 1 : x - 1;
          int adj_y = corner_y ? y + 1 : y - 1;

          // treat edges as water
          // if there is no adjacent land in either direction, round the (exterior/obtuse) corner
          if ((adj_x < 0 || adj_x >= num_blocks_w || grid_flags[to_pos(adj_x, y)] & WATER) &&
            (adj_y < 0 || adj_y >= num_blocks_h || grid_flags[to_pos(x, adj_y)] & WATER)) {
              render_corner(renderer, sprites, 6 + corner_x, 0 + corner_y, x * 2 + corner_x, y * 2 + corner_y);
          }
          else {
            SDL_Rect land_rect = {
              .x = x * block_w + corner_x * block_w/2 - vp.x,
              .y = y * block_h + corner_y * block_h/2 - vp.y,
              .w = block_w/2,
              .h = block_h/2
            };
            if (SDL_RenderFillRect(renderer, &land_rect) < 0)
              error("filling land rect");
          }
        }
      }
    }
  }

  for (int i = 0; i < max_blocks; ++i) {
    if (blocks[i].flags & DELETED)
      continue;

    render_sprite(renderer, sprites, 1,1, blocks[i].x,blocks[i].y);
  }

  for (int i = 0; i < max_power_stones; ++i)
    render_sprite(renderer, sprites, 1,0, power_stones[i].x,power_stones[i].y);

  for (int i = 0; i < max_turrets; ++i) {
    if (turrets[i].flags & DELETED)
      continue;

    if (turrets[i].flags & POWER)
      render_sprite(renderer, sprites, 2,0, turrets[i].x,turrets[i].y);
    else
      render_sprite(renderer, sprites, 0,0, turrets[i].x,turrets[i].y);
  }

  if (SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) < 0)
    error("setting Bullet color");
  for (int i = 0; i < max_bullets; ++i) {
    if (bullets[i].flags & DELETED)
      continue;
    
    int x = bullets[i].x - vp.x;
    int y = bullets[i].y - vp.y;
    SDL_Rect bullet_rect = {
      .x = x,
      .y = y,
      .w = bullet_w,
      .h = bullet_h
    };
    if (SDL_RenderFillRect(renderer, &bullet_rect) < 0)
      error("filling bullet rect");
  }

  for (int i = 0; i < max_beasts; ++i) {
    if (beasts[i].flags & DELETED)
      continue;

    int sprite_x_pos = 0;
    int sprite_y_pos = 1;
    if (beasts[i].flags & SUPER)
      sprite_x_pos = 2;
    if (grid_flags[to_pos(beasts[i].x, beasts[i].y)] & WATER)
      sprite_y_pos += 1;

    render_sprite(renderer, sprites, sprite_x_pos,sprite_y_pos, beasts[i].x, beasts[i].y);
  }

  // header
  int text_px_size = 2;
  if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0)
    error("setting header color");
  SDL_Rect header_rect = {
    .x = 0,
    .y = 0,
    .w = vp.w,
    .h = 75
  };
  if (SDL_RenderFillRect(renderer, &header_rect) < 0)
    error("filling header rect");

  int control_x = vp.w/2 - ui_bar_img->w/2;
  ui_bar_img->x = control_x;
  render_img(renderer, ui_bar_img);

  if (SDL_SetRenderDrawColor(renderer, 59, 59, 59, 255) < 0)
    error("setting filled coin bar color");

  double coin_bar_len = 0;
  if (num_collected_blocks <= 50)
    coin_bar_len = num_collected_blocks * 5;
  else if (num_collected_blocks <= 100)
    coin_bar_len = 50.0 * 5.0 + (num_collected_blocks - 50.0) * 2.5;
  else if (num_collected_blocks <= 150)
    coin_bar_len = 50.0 * 5.0 + 100.0 * 2.5 + (num_collected_blocks - 100.0) * 1.25;
  else if (num_collected_blocks <= 200)
    coin_bar_len = 50.0 * 5.0 + 100.0 * 2.5 + 150.0 * 1.25 + (num_collected_blocks - 150.0) * 0.625;
  else
    coin_bar_len = 50.0 * 5.0 + 100.0 * 2.5 + 150 * 0.625;
  // TODO: fill this out more & make it programmatic

  SDL_Rect coin_bar_rect = {
    .x = control_x + 10,
    .y = 60,
    .w = (int)coin_bar_len,
    .h = 5
  };
  if (SDL_RenderFillRect(renderer, &coin_bar_rect) < 0)
    error("filling header rect");

  if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170) < 0)
    error("setting disabled overlay color");

  if (num_collected_blocks < num_blocks_per_road) {
    SDL_Rect disabled_overlay = {.x = control_x + 5, .y = 5, .w = 50, .h = 50};
    if (SDL_RenderFillRect(renderer, &disabled_overlay) < 0)
      error("filling disabled overlay");
  }

  if (num_collected_blocks < num_blocks_per_turret) {
    SDL_Rect disabled_overlay = {.x = control_x + 98, .y = 5, .w = 50, .h = 50};
    if (SDL_RenderFillRect(renderer, &disabled_overlay) < 0)
      error("filling disabled overlay");
  }

  if (num_collected_blocks < num_blocks_per_bridge) {
    SDL_Rect disabled_overlay = {.x = control_x + 184, .y = 5, .w = 50, .h = 50};
    if (SDL_RenderFillRect(renderer, &disabled_overlay) < 0)
      error("filling disabled overlay");
  }

  SDL_RenderPresent(renderer);
}


// Grid Functions

bool in_bounds(int x, int y) {
  return x >= 0 && x < num_blocks_w &&
    y >= 0 && y < num_blocks_h;
}

int find_avail_pos(Entity* grid[], byte grid_flags[]) {
  int x;
  int y;
  int pos;
  do {
    x = rand() % num_blocks_w;
    y = rand() % num_blocks_h;
    pos = to_pos(x, y);
  } while (grid[pos] || grid_flags[pos] & WATER);
  return pos;
}

void move(Entity* ent, Entity* grid[], int x, int y) {
  remove_from_grid(ent, grid);
  set_xy(ent, grid, x, y);
}

void set_pos(Entity* ent, Entity* grid[], int pos) {
  set_xy(ent, grid, to_x(pos), to_y(pos));
}

void set_xy(Entity* ent, Entity* grid[], int x, int y) {
  ent->x = x;
  ent->y = y;
  grid[to_pos(x, y)] = ent;
}

void remove_from_grid(Entity* ent, Entity* grid[]) {
  int prev_pos = to_pos(ent->x, ent->y);
  grid[prev_pos] = NULL;
}

int to_x(int ix) {
  return ix % num_blocks_w;
}

int to_y(int ix) {
  return ix / num_blocks_w;
}

int to_pos(int x, int y) {
  int pos = x + y * num_blocks_w;
  if (pos < 0)
    error("position out of bounds (negative)");
  if (pos >= grid_len)
    error("position out of bounds (greater than grid size)");
  return pos;
}

bool is_in_grid(int x, int y) {
  if ((x < 0 || x >= num_blocks_w) ||
    (y < 0 || y >= num_blocks_h))
      return false;

  return true;
}


// Game-Specific Functions

bool is_next_to_wall(Entity* beast, Entity* grid[]) {
  for (int dir_x = -1; dir_x <= 1; ++dir_x) {
    for (int dir_y = -1; dir_y <= 1; ++dir_y) {
      // check the bounds
      int new_x = beast->x + dir_x;
      int new_y = beast->y + dir_y;
      if (!is_in_grid(new_x, new_y))
        continue;

      int pos = to_pos(new_x, new_y);
      if (grid[pos] && grid[pos]->flags & POWER)
        return true;
    }
  }
  return false;
}

void beast_explode(Entity* beast, Entity* grid[]) {
  int x = beast->x;
  int y = beast->y;

  if (!(beast->flags & SUPER))
    del_entity(beast, grid);

  for (int dir_x = -1; dir_x <= 1; ++dir_x) {
    for (int dir_y = -1; dir_y <= 1; ++dir_y) {
      // check the bounds
      int new_x = x + dir_x;
      int new_y = y + dir_y;
      if (!is_in_grid(new_x, new_y))
        continue;

      int pos = to_pos(new_x, new_y);
      Entity* ent = grid[pos];
      if (ent && ent->flags & BLOCK && !(ent->flags & STONE))
        del_entity(ent, grid);
    }
  }
}

Entity* closest_entity(int x, int y, Entity entities[], int num_entities) {
  Entity* winner = NULL;
  double winner_dist = -1;
  
  for (int i = 0; i < num_entities; ++i) {
    if (entities[i].flags & DELETED)
      continue;

    double dist = calc_dist(x, y, entities[i].x, entities[i].y);
    if (winner_dist == -1 || dist < winner_dist) {
      winner = &entities[i];
      winner_dist = dist;
    }
  }
  return winner;
}

void del_entity(Entity* ent, Entity* grid[]) {
  ent->flags |= DELETED; // flip DELETED bit on
  ent->flags &= (~POWER); // clear POWER flag since the block will be re-used
  remove_from_grid(ent, grid);
}

void update_powered_turrets(Entity* grid[], Entity power_stones[]) {
  // clear POWER bit everywhere on the grid
  for (int i = 0; i < grid_len; ++i)
    if (grid[i] && grid[i]->flags & POWER)
      grid[i]->flags &= (~POWER);

  for (int i = 0; i < max_power_stones; ++i) {
    int x = power_stones[i].x;
    int y = power_stones[i].y;

    set_powered(grid, x, y);
  }
}

void set_powered(Entity* grid[], int x, int y) {
  if (!is_in_grid(x, y))
    return;

  Entity* ent = grid[to_pos(x, y)];
  if (!ent || !(ent->flags & BLOCK) || ent->flags & POWER)
    return;

  if (!(ent->flags & TURRET) && !(ent->flags & STONE))
    return;

  ent->flags |= POWER;

  set_powered(grid, x + 1, y);
  set_powered(grid, x - 1, y);
  set_powered(grid, x, y + 1);
  set_powered(grid, x, y - 1);
}

int get_move_pos(Entity* beast, Entity turrets[], Entity* grid[]) {
  Entity* turret = NULL;
  double closest_dist = attack_dist;

  for (int i = 0; i < max_turrets; ++i) {
    if (turrets[i].flags & DELETED)
      continue;

    double dist = calc_dist(turrets[i].x, turrets[i].y, beast->x, beast->y);
    if (dist < closest_dist) {
      closest_dist = dist;
      turret = &turrets[i];
    }
  }


  int x = beast->x;
  int y = beast->y;

  int dir_x = 0;
  int dir_y = 0;
  if (turret && turret->x < x)
    dir_x = -1;
  else if (turret && turret->x > x)
    dir_x = 1;

  if (turret && turret->y < y)
    dir_y = -1;
  else if (turret && turret->y > y)
    dir_y = 1;

  bool found_direction = true;

  // a quarter of the time we want them to move randomly
  // this keeps them from being too deterministic & from getting stuck
  // behind rocks, etc
  bool move_randomly = !turret || rand() % 100 > 75;

  // the beast will "attack" the turret on this move; don't move away
  if (turret && abs(turret->x - x) <= 1 && abs(turret->y - y) <= 1)
    return to_pos(x, y);
  
  // try to move towards the fortress, if possible
  if (!move_randomly && dir_x && dir_y && in_bounds(x + dir_x, y + dir_y) && !grid[to_pos(x + dir_x, y + dir_y)]) {
    x += dir_x;
    y += dir_y;
  }
  else if (!move_randomly && dir_x && in_bounds(x + dir_x, y) && !grid[to_pos(x + dir_x, y)]) {
    x += dir_x;
  }
  else if (!move_randomly && dir_y && in_bounds(x, y + dir_y) && !grid[to_pos(x, y + dir_y)]) {
    y += dir_y;
  }
  // if there's no delta in one dimension, try +/- 1
  else if (!move_randomly && !dir_x && in_bounds(x + 1, y + dir_y) && !grid[to_pos(x + 1, y + dir_y)]) {
    x += 1;
    y += dir_y;
  }
  else if (!move_randomly && !dir_x && in_bounds(x - 1, y + dir_y) && !grid[to_pos(x - 1, y + dir_y)]) {
    x -= 1;
    y += dir_y;
  }
  else if (!move_randomly && !dir_y && in_bounds(x + dir_x, y + 1) && !grid[to_pos(x + dir_x, y + 1)]) {
    x += dir_x;
    y += 1;
  }
  else if (!move_randomly && !dir_y && in_bounds(x + dir_x, y - 1) && !grid[to_pos(x + dir_x, y - 1)]) {
    x += dir_x;
    y -= 1;
  }
  else {
    // test all combinations of directions
    found_direction = false;
    for (int mv_x = -1; mv_x <= 1; ++mv_x) {
      if (!found_direction) {
        for (int mv_y = -1; mv_y <= 1; ++mv_y) {
          if (!mv_x && !mv_y)
            continue; // 0,0 isn't a real move

          if (in_bounds(x + mv_x, y + mv_y) && !grid[to_pos(x + mv_x, y + mv_y)]) {
            found_direction = true;
            break;
          }
        }
      }
    }
    if (found_direction) {
      int mv_x = -1;
      int mv_y = -1;
      do {
        int dir = rand() % 8; // there are 8 possible directions

        if (dir == 0) {
          mv_x = -1;
          mv_y = -1;
        }
        else if (dir == 1) {
          mv_x = 0;
          mv_y = -1;
        }
        else if (dir == 2) {
          mv_x = 1;
          mv_y = -1;
        }
        else if (dir == 3) {
          mv_x = -1;
          mv_y = 0;
        }
        else if (dir == 4) {
          mv_x = 1;
          mv_y = 0;
        }
        else if (dir == 5) {
          mv_x = -1;
          mv_y = 1;
        }
        else if (dir == 6) {
          mv_x = 0;
          mv_y = 1;
        }
        else { // dir == 7
          mv_x = 1;
          mv_y = 1;
        }
      } while (in_bounds(x + mv_x, y + mv_y) && grid[to_pos(x + mv_x, y + mv_y)]);
      x += mv_x;
      y += mv_y;
    }
  }

  if (!found_direction || !in_bounds(x, y))
    return -1;
  else
    return to_pos(x, y);
}


// Generic Functions

void toggle_fullscreen(SDL_Window *win) {
  Uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) || (flags & SDL_WINDOW_FULLSCREEN))
    flags = 0;
  else
    flags = SDL_WINDOW_FULLSCREEN;

  if (SDL_SetWindowFullscreen(win, flags) < 0)
    error("Toggling fullscreen mode failed");
}

double calc_dist(int x1, int y1, int x2, int y2) {
  return sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2));
}

int clamp(int val, int min, int max) {
  if (val < min)
    return min;
  else if (val > max)
    return max;
  else
    return val;
}

int render_text(SDL_Renderer* renderer, char str[], int offset_x, int offset_y, int size) {
  int i;
  for (i = 0; str[i] != '\0'; ++i) {
    int code = str[i];
    if (code < 0 || code > 127)
      error("Text code out of range");

    char* bitmap = font8x8_basic[code];
    int set = 0;
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        set = bitmap[y] & 1 << x;
        if (!set)
          continue;

        SDL_Rect r = {
          .x = offset_x + i * (size) * 8 + x * size,
          .y = offset_y + y * size,
          .w = size,
          .h = size
        };
        if (SDL_RenderFillRect(renderer, &r) < 0)
          error("drawing text block");
      }
    }
  }

  // width of total text string
  return i * size * 8;
}

// TODO: it's probably a little more efficient to load the image into an sdl image
// then get the dimensions, then load it into a texture
// instead of loading it directly to a texture & then querying the texture...
Image load_img(SDL_Renderer* renderer, char* path) {
  Image img = {};
  img.tex = IMG_LoadTexture(renderer, path);
  SDL_QueryTexture(img.tex, NULL, NULL, &img.w, &img.h);
  return img;
}

void render_img(SDL_Renderer* renderer, Image* img) {
  SDL_Rect r = {.x = img->x, .y = img->y, .w = img->w, .h = img->h};
  if (SDL_RenderCopy(renderer, img->tex, NULL, &r) < 0)
    error("renderCopy");
}

// centers the image horizontally in the viewport
void center_img(Image* img, Viewport* viewport) {
  img->x = viewport->w / 2 - img->w / 2;
}

void render_sprite(SDL_Renderer* renderer, SDL_Texture* sprites, int src_x, int src_y, int dest_x, int dest_y) {
  SDL_Rect src = {.x = src_x * block_w, .y = src_y * block_h, .w = block_w, .h = block_h};
  SDL_Rect dest = {.x = dest_x * block_w - vp.x, .y = dest_y * block_h - vp.y, .w = block_w, .h = block_h};
  if (SDL_RenderCopy(renderer, sprites, &src, &dest) < 0)
    error("renderCopy");
}

void render_corner(SDL_Renderer* renderer, SDL_Texture* sprites, int src_x, int src_y, int dest_x, int dest_y) {
  SDL_Rect src = {.x = src_x * block_w/2, .y = src_y * block_h/2, .w = block_w/2, .h = block_h/2};
  SDL_Rect dest = {.x = dest_x * block_w/2 - vp.x, .y = dest_y * block_h/2 - vp.y, .w = block_w/2, .h = block_h/2};
  if (SDL_RenderCopy(renderer, sprites, &src, &dest) < 0)
    error("renderCopy");
}

bool is_mouseover(Image* img, int x, int y) {
  return x >= img->x && x <= (img->x + img->w) &&
    y >= img->y && y <= (img->y + img->h);
}

void error(char* activity) {
  printf("%s failed: %s\n", activity, SDL_GetError());
  SDL_Quit();
  exit(-1);
}
