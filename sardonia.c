#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "SDL.h"
#include "font8x8_basic.h"

typedef unsigned char byte;

#define DELETED 0x1
#define BLOCK 0x2
#define BEAST 0x4
#define PLAYER 0x8
#define STATIC 0x10
#define TURRET 0x20
#define POWER 0x40
#define SUPER 0x80

#define PICKING_UP 0
#define PLACING 1

short mode = PICKING_UP;
int num_collected_blocks = 10;
int block_ratio = 2; // you have to collect 2 rocks to build 1 wall
int num_blocks_per_turret = 25; // collect 5 rocks to build 1 turret
int attack_dist = 30; // how close a beast has to be before he moves towards you
byte beast_health = 3;

typedef struct {
  byte flags;
  byte health;
  int x;
  int y;
} entity;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} viewport;

typedef struct {
  byte flags;
  double x;
  double y;
  double dx;
  double dy;
} bullet;

// grid functions
int find_avail_pos(entity* grid[]);
void move(entity* ent, entity* grid[], int x, int y);
void set_pos(entity* ent, entity* grid[], int pos);
void set_xy(entity* ent, entity* grid[], int x, int y);
void remove_from_grid(entity* ent, entity* grid[]);
int to_x(int ix);
int to_y(int ix);
int to_pos(int x, int y);
bool is_in_grid(int x, int y);

// game-specific functions
bool is_next_to_wall(entity* beast, entity* grid[]);
void beast_explode(entity* beast, entity* grid[]);
entity* closest_entity(int x, int y, entity entities[], int num_entities);
void del_entity(entity* ent, entity* grid[]);
void update_powered_walls(entity* grid[], entity static_blocks[], int num_static_blocks);
void set_powered(entity* grid[], int x, int y);
int get_move_pos(entity* beast, entity* player, entity* grid[]);

// generic functions
void toggle_fullscreen(SDL_Window *win);
bool in_bounds(int x, int y);
double calc_dist(int x1, int y1, int x2, int y2);
int render_text(SDL_Renderer* renderer, char str[], int offset_x, int offset_y, int size);
void error(char* activity);

int block_w = 40;
int block_h = 40;
int bullet_w = 4;
int bullet_h = 4;
double bullet_speed = 300.0; // in px/sec
int block_density_pct = 20;
int starting_distance = 15;

entity player = {.flags = PLAYER, .x = 1, .y = 1};

viewport vp = {
  .x = 0,
  .y = 0,
  .w = 0,
  .h = 0
};

int num_blocks_w = 40 * 3;
int num_blocks_h = 30 * 3;
int grid_len;

unsigned int last_move_time = 0;
int beast_speed = 500; // ms between moves
unsigned int last_fire_time = 0;
int turret_fire_interval = 2000; // ms between turret firing
int mine_interval = 10000; // ms between mine generating metal
const int num_beasts = 10;
int max_turrets = 50;
int max_bullets = 100;

int main(int num_args, char* args[]) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    error("initializing SDL");

  srand(time(NULL));

  grid_len = num_blocks_w * num_blocks_h;
  entity* grid[grid_len];
  byte grid_flags[grid_len];
  for (int i = 0; i < grid_len; ++i) {
    grid[i] = NULL;
    grid_flags[i] = 0;
  }

  int num_static_blocks = num_blocks_w * 2 + (num_blocks_h - 2) * 2 + 10;
  entity static_blocks[num_static_blocks];
  int ix = 0;
  for (int x = 0; x < num_blocks_w; ++x) {
    // top row
    static_blocks[ix].flags = (BLOCK | STATIC);
    set_xy(&static_blocks[ix], grid, x, 0);
    ix++;

    // bottom row
    static_blocks[ix].flags = (BLOCK | STATIC);
    set_xy(&static_blocks[ix], grid, x, num_blocks_h - 1);
    ix++;
  }

  for (int y = 1; y < num_blocks_h - 1; ++y) {
    // left row
    static_blocks[ix].flags = (BLOCK | STATIC);
    set_xy(&static_blocks[ix], grid, 0, y);
    ix++;

    // right row
    static_blocks[ix].flags = (BLOCK | STATIC);
    set_xy(&static_blocks[ix], grid, num_blocks_w - 1, y);
    ix++;
  }

  // set the position of the 1st player
  set_pos(&player, grid, find_avail_pos(grid));

  // additional 10 static blocks in the playing field
  for (int i = 0; i < 10; ++i) {
    int pos = find_avail_pos(grid);
    static_blocks[ix].flags = (BLOCK | STATIC);
    static_blocks[ix].x = to_x(pos);
    static_blocks[ix].y = to_y(pos);
    grid[pos] = &static_blocks[ix];
    ix++;
  }

  // DRY violation: consolidate below into place_entities()
  // Doing x3 to go from 20% density to 60% due to the mines
  // really we should just have one object for every possible grid position...
  int num_blocks = grid_len * block_density_pct * 3 / 100;
  entity blocks[num_blocks];
  for (int i = 0; i < num_blocks; ++i) {
    if (i < grid_len * block_density_pct / 100) {
      int pos = find_avail_pos(grid);
      blocks[i].flags = BLOCK;
      blocks[i].x = to_x(pos);
      blocks[i].y = to_y(pos);
      grid[pos] = &blocks[i];
    }
    else {
      blocks[i].flags = BLOCK | DELETED;
    }
  }

  entity beasts[num_beasts];
  for (int i = 0; i < num_beasts; ++i) {
    int pos;
    do {
      pos = find_avail_pos(grid);
    } while (abs(player.x - to_x(pos)) < starting_distance && abs(player.y - to_y(pos)) < starting_distance);

    beasts[i].flags = BEAST;
    if (i == 0)
      beasts[i].flags |= SUPER;

    beasts[i].x = to_x(pos);
    beasts[i].y = to_y(pos);
    beasts[i].health = beast_health;
    grid[pos] = &beasts[i];
  }

  // precreate all turrets "blocks" as deleted
  entity turrets[max_turrets];
  for (int i = 0; i < max_turrets; ++i)
    turrets[i].flags = BLOCK | TURRET | DELETED;

  bullet bullets[max_bullets];
  for (int i = 0; i < max_bullets; ++i)
    bullets[i].flags = DELETED;

  SDL_Window *window;
  window = SDL_CreateWindow("Beast", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, num_blocks_w * block_w, num_blocks_h * block_h, SDL_WINDOW_RESIZABLE);
  if (!window)
    error("creating window");

  SDL_GetWindowSize(window, &vp.w, &vp.h);

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  bool is_gameover = false;
  int dir_x = 0;
  int dir_y = 0;
  unsigned int last_loop_time = SDL_GetTicks();
  while (!is_gameover) {
    unsigned int curr_time = SDL_GetTicks();
    double dt = (curr_time - last_loop_time) / 1000.0; // dt should always be in seconds
    last_loop_time = curr_time;

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    bool is_spacebar_pressed = state[SDL_SCANCODE_SPACE];
    
    SDL_Event evt;
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
          if (evt.motion.state & SDL_BUTTON_LMASK) {
            int x = (evt.button.x + vp.x) / block_w;
            int y = (evt.button.y + vp.y) / block_h;
            int pos = to_pos(x, y);
            if (grid[pos] && mode == PICKING_UP && grid[pos]->flags & BLOCK && !(grid[pos]->flags & STATIC)) {
              // DRY violation w/ below (MOUSEBUTTONDOWN handler)
              num_collected_blocks++;
              del_entity(grid[pos], grid);
            }
            else if (!grid[pos] && mode == PLACING && num_collected_blocks >= block_ratio) {
              // DRY violation w/ below (MOUSEBUTTONDOWN handler)
              for (int i = 0; i < num_blocks; ++i) {
                if (blocks[i].flags & DELETED) {
                  num_collected_blocks -= block_ratio;
                  blocks[i].flags &= (~DELETED); // clear deleted bit
                  set_xy(&blocks[i], grid, x, y);
                  update_powered_walls(grid, static_blocks, num_static_blocks);
                  break;
                }
              }
            }
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (evt.button.button == SDL_BUTTON_LEFT) {
            int x = (evt.button.x + vp.x) / block_w;
            int y = (evt.button.y + vp.y) / block_h;
            int pos = to_pos(x, y);
            if (grid[pos] && (grid[pos]->flags & BLOCK) &&
              !(grid[pos]->flags & STATIC)) {
                mode = PICKING_UP;
                num_collected_blocks++;
                del_entity(grid[pos], grid);
            }
            else if (!grid[pos] && num_collected_blocks >= block_ratio) {
              mode = PLACING;
              for (int i = 0; i < num_blocks; ++i) {
                if (blocks[i].flags & DELETED) {
                  num_collected_blocks -= block_ratio;
                  blocks[i].flags &= (~DELETED); // clear deleted bit
                  set_xy(&blocks[i], grid, x, y);
                  update_powered_walls(grid, static_blocks, num_static_blocks);
                  break;
                }
              }
            }
          }
          else if (evt.button.button == SDL_BUTTON_RIGHT) {
            int x = (evt.button.x + vp.x) / block_w;
            int y = (evt.button.y + vp.y) / block_h;
            int pos = to_pos(x, y);
            if (!grid[pos] && num_collected_blocks >= num_blocks_per_turret) {
              for (int i = 0; i < max_turrets; ++i) {
                if (turrets[i].flags & DELETED) {
                  num_collected_blocks -= num_blocks_per_turret;
                  turrets[i].flags &= (~DELETED); // clear deleted bit
                  set_xy(&turrets[i], grid, x, y);
                  break;
                }
              }
              // TODO: how do we determine if max_turrets has been reached
              // and alert the player?
            }
          }
          break;
        case SDL_KEYDOWN:
          dir_x = 0;
          dir_y = 0;
          switch (evt.key.keysym.sym) {
            case SDLK_ESCAPE:
              is_gameover = true;
              break;
            case SDLK_LEFT:
              dir_x = -1;
              break;
            case SDLK_RIGHT:
              dir_x = 1;
              break;
            case SDLK_UP:
              dir_y = -1;
              break;
            case SDLK_DOWN:
              dir_y = 1;
              break;
            case SDLK_f:
              toggle_fullscreen(window);
              break;
          }

          if ((dir_x || dir_y) && !grid[to_pos(player.x + dir_x, player.y + dir_y)])
            move(&player, grid, player.x + dir_x, player.y + dir_y);
        break;
      }
    }

    // shift the viewport if necessary, to include the player
    int player_x = player.x * block_w;
    int player_y = player.y * block_h;
    int left_edge = vp.x;
    int right_edge = vp.w + vp.x;
    int top_edge = vp.y;
    int bottom_edge = vp.h + vp.y;
    int h_padding = 10 * block_w;
    int v_padding = 10 * block_h;

    // smooth viewport following (cuts the distance by a tenth each frame)
    if (player_x > right_edge - h_padding)
      vp.x += (player_x - (right_edge - h_padding)) / 10;
    else if (player_x < left_edge + h_padding)
      vp.x -= ((left_edge + h_padding) - player_x) / 10;
    if (player_y > bottom_edge - v_padding)
      vp.y += (player_y - (bottom_edge - v_padding)) / 10;
    else if (player_y < top_edge + v_padding)
      vp.y -= ((top_edge + v_padding) - player_y) / 10;

    // set BG color
    if (SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255) < 0)
      error("setting bg color");
    if (SDL_RenderClear(renderer) < 0)
      error("clearing renderer");

    for (int i = 0; i < num_blocks; ++i) {
      if (blocks[i].flags & DELETED)
        continue;

      if (blocks[i].flags & POWER) {
        if (SDL_SetRenderDrawColor(renderer, 170, 160, 120, 255) < 0)
          error("setting powered border color");
      }
      else {
        if (SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255) < 0)
          error("setting block color");
      }

      SDL_Rect r = {
        .x = blocks[i].x * block_w - vp.x,
        .y = blocks[i].y * block_h - vp.y,
        .w = block_w,
        .h = block_h
      };
      if (SDL_RenderFillRect(renderer, &r) < 0)
        error("drawing block");
    }

    if (SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255) < 0)
      error("setting static block color");

    for (int i = 0; i < num_static_blocks; ++i) {
      // don't render the border blocks
      if (static_blocks[i].x == 0 || static_blocks[i].x == num_blocks_w - 1 ||
        static_blocks[i].y == 0 || static_blocks[i].y == num_blocks_h - 1)
        continue;

      SDL_Rect r = {
        .x = static_blocks[i].x * block_w - vp.x,
        .y = static_blocks[i].y * block_h - vp.y,
        .w = block_w,
        .h = block_h
      };
      if (SDL_RenderFillRect(renderer, &r) < 0)
        error("drawing block");
    }

    if (SDL_SetRenderDrawColor(renderer, 170, 230, 240, 255) < 0)
      error("setting turret color");
    for (int i = 0; i < max_turrets; ++i) {
      if (turrets[i].flags & DELETED)
        continue;

      SDL_Rect turret_rect = {
        .x = turrets[i].x * block_w - vp.x,
        .y = turrets[i].y * block_h - vp.y,
        .w = block_w,
        .h = block_h
      };
      if (SDL_RenderFillRect(renderer, &turret_rect) < 0)
        error("filling rect");
    }

    if (SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) < 0)
      error("setting bullet color");
    for (int i = 0; i < max_bullets; ++i) {
      if (bullets[i].flags & DELETED)
        continue;

      // TODO: move this from drawing to an update fn
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
        entity* ent = grid[pos];
        if (ent && ent->flags & BLOCK) {
          bullets[i].flags |= DELETED;
          continue;
        }
        else if (ent && ent->flags & BEAST) {
          // if it's a super bullet or NOT a super-beast, "kill the beast!"
          if (bullets[i].flags & SUPER || !(ent->flags & SUPER)) {
            ent->health--;
            if (!ent->health)
              del_entity(ent, grid);
          }
          bullets[i].flags |= DELETED;
        }
      }
      
      int x = bullets[i].x - vp.x;
      int y = bullets[i].y - vp.y;
      SDL_Rect bullet_rect = {
        .x = x,
        .y = y,
        .w = bullet_w,
        .h = bullet_h
      };
      if (SDL_RenderFillRect(renderer, &bullet_rect) < 0)
        error("filling rect");
    }

    if (SDL_SetRenderDrawColor(renderer, 140, 60, 140, 255) < 0)
      error("setting player color");
    SDL_Rect player_rect = {
      .x = player.x * block_w - vp.x,
      .y = player.y * block_h - vp.y,
      .w = block_w,
      .h = block_h
    };
    if (SDL_RenderFillRect(renderer, &player_rect) < 0)
      error("filling rect");

    for (int i = 0; i < num_beasts; ++i) {
      if (beasts[i].flags & DELETED)
        continue;

      if (beasts[i].flags & SUPER) {
        if (SDL_SetRenderDrawColor(renderer, 225, 30, 30, 255) < 0)
          error("setting super beast color");
      }
      else {
        if (SDL_SetRenderDrawColor(renderer, 140, 60, 60, 255) < 0)
          error("setting beast color");
      }

      SDL_Rect beast_rect = {
        .x = beasts[i].x * block_w - vp.x,
        .y = beasts[i].y * block_h - vp.y,
        .w = block_w,
        .h = block_h
      };
      if (SDL_RenderFillRect(renderer, &beast_rect) < 0)
        error("filling beast rect");
    }

    // header
    int text_px_size = 2;
    if (SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) < 0)
      error("setting header color");
    SDL_Rect beast_rect = {
      .x = 0,
      .y = 0,
      .w = vp.w,
      .h = text_px_size * 8 + 4
    };
    if (SDL_RenderFillRect(renderer, &beast_rect) < 0)
      error("filling beast rect");

    if (SDL_SetRenderDrawColor(renderer, 140, 60, 60, 255) < 0)
      error("setting header text color");

    int num_mines = 0;
    for (int i = 0; i < max_turrets; ++i)
      if (!(turrets[i].flags & DELETED))
        num_mines++;

    char resource_str[24];
    snprintf(resource_str, sizeof(resource_str), "Mines: %d, Metal: %d", num_mines, num_collected_blocks);
    int x_pos = render_text(renderer, resource_str, 2, 2, text_px_size);

    if (curr_time - last_fire_time >= mine_interval) {
      for (int i = 0; i < max_turrets; ++i) {
        entity* turret = &turrets[i];
        if (!(turret->flags & DELETED))
          num_collected_blocks++;
      }
      last_fire_time = curr_time;
    }

    // if (curr_time - last_fire_time >= turret_fire_interval) {
    //   for (int i = 0; i < max_turrets; ++i) {
    //     entity* turret = &turrets[i];
    //     if (turret->flags & DELETED)
    //       continue;

    //     // only turrets that are part of an enclosure can fire
    //     int pos = to_pos(turret->x, turret->y);
    //     if (!(turret->flags & POWER))
    //       continue;

    //     entity* beast = closest_entity(turret->x, turret->y, beasts, num_beasts);
    //     if (beast) {
    //       double dist = calc_dist(beast->x, beast->y, turret->x, turret->y);
    //       // dividing by the distance gives us a normalized 1-unit vector
    //       double dx = (beast->x - turret->x) / dist;
    //       double dy = (beast->y - turret->y) / dist;
    //       for (int j = 0; j < max_bullets; ++j) {
    //         bullet* b = &bullets[j];
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

    if (curr_time - last_move_time >= beast_speed) {
      for (int i = 0; i < num_beasts; ++i) {
        if (beasts[i].flags & DELETED)
          continue;

        if (is_next_to_wall(&beasts[i], grid)) {
          if (beasts[i].flags & SUPER || rand() % 100 >= 98) {
            beast_explode(&beasts[i], grid);
            continue;
          }
        }

        int dest_pos = get_move_pos(&beasts[i], &player, grid);

        // if the beast is surrounded by blocks & has nowhere to move, it blows up
        if (dest_pos == -1)
          beast_explode(&beasts[i], grid);
        else
          move(&beasts[i], grid, to_x(dest_pos), to_y(dest_pos));
        
        if (beasts[i].x == player.x && beasts[i].y == player.y)
          is_gameover = true;
      }
      last_move_time = curr_time;
    }
    
    SDL_RenderPresent(renderer);
    SDL_Delay(10);
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

// Grid Functions

bool in_bounds(int x, int y) {
  return x >= 0 && x < num_blocks_w &&
    y >= 0 && y < num_blocks_h;
}

int find_avail_pos(entity* grid[]) {
  int x;
  int y;
  int pos;
  do {
    x = rand() % num_blocks_w;
    y = rand() % num_blocks_h;
    pos = to_pos(x, y);
  } while (grid[pos]);
  return pos;
}

void move(entity* ent, entity* grid[], int x, int y) {
  remove_from_grid(ent, grid);
  set_xy(ent, grid, x, y);
}

void set_pos(entity* ent, entity* grid[], int pos) {
  set_xy(ent, grid, to_x(pos), to_y(pos));
}

void set_xy(entity* ent, entity* grid[], int x, int y) {
  ent->x = x;
  ent->y = y;
  grid[to_pos(x, y)] = ent;
}

void remove_from_grid(entity* ent, entity* grid[]) {
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

bool is_next_to_wall(entity* beast, entity* grid[]) {
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

void beast_explode(entity* beast, entity* grid[]) {
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
      entity* ent = grid[pos];
      if (ent && ent->flags & BLOCK && !(ent->flags & STATIC))
        del_entity(ent, grid);
    }
  }
}

entity* closest_entity(int x, int y, entity entities[], int num_entities) {
  entity* winner = NULL;
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

void del_entity(entity* ent, entity* grid[]) {
  ent->flags |= DELETED; // flip DELETED bit on
  ent->flags &= (~POWER); // clear POWER flag since the block will be re-used
  remove_from_grid(ent, grid);
}

void update_powered_walls(entity* grid[], entity static_blocks[], int num_static_blocks) {
  // clear POWER bit everywhere on the grid
  for (int i = 0; i < grid_len; ++i)
    if (grid[i] && grid[i]->flags & POWER)
      grid[i]->flags &= (~POWER);

  for (int i = 0; i < num_static_blocks; ++i) {
    int x = static_blocks[i].x;
    int y = static_blocks[i].y;
    
    // skip rows of static blocks around edges
    if (x == 0 || x == num_blocks_w - 1 ||
      y == 0 || y == num_blocks_h - 1)
        continue;

    set_powered(grid, x, y);
  }
}

void set_powered(entity* grid[], int x, int y) {
  if (!is_in_grid(x, y))
    return;

  entity* ent = grid[to_pos(x, y)];
  if (!ent || !(ent->flags & BLOCK) || ent->flags & POWER)
    return;

  ent->flags |= POWER;

  set_powered(grid, x + 1, y);
  set_powered(grid, x - 1, y);
  set_powered(grid, x, y + 1);
  set_powered(grid, x, y - 1);
}

int get_move_pos(entity* beast, entity* player, entity* grid[]) {
  int x = beast->x;
  int y = beast->y;

  int dir_x = 0;
  int dir_y = 0;
  if (player->x < x)
    dir_x = -1;
  else if (player->x > x)
    dir_x = 1;

  if (player->y < y)
    dir_y = -1;
  else if (player->y > y)
    dir_y = 1;

  bool found_direction = true;

  // a quarter of the time we want them to move randomly
  // this keeps them from being too deterministic & from getting stuck
  // behind walls, etc
  bool move_randomly = rand() % 100 > 75;

  // if the beast isn't within the attack distance, it should move randomly
  double dist = calc_dist(player->x, player->y, beast->x, beast->y);
  if (dist > attack_dist)
    move_randomly = true;

  // the beast will "get" the player on this move
  if (abs(player->x - x) <= 1 && abs(player->y - y) <= 1) {
    x = player->x;
    y = player->y;
  }
  // try to move towards the player, if possible
  else if (!move_randomly && dir_x && dir_y && !grid[to_pos(x + dir_x, y + dir_y)]) {
    x += dir_x;
    y += dir_y;
  }
  else if (!move_randomly && dir_x && !grid[to_pos(x + dir_x, y)]) {
    x += dir_x;
  }
  else if (!move_randomly && dir_y && !grid[to_pos(x, y + dir_y)]) {
    y += dir_y;
  }
  // if there's no delta in one dimension, try +/- 1
  else if (!move_randomly && !dir_x && !grid[to_pos(x + 1, y + dir_y)]) {
    x += 1;
    y += dir_y;
  }
  else if (!move_randomly && !dir_x && !grid[to_pos(x - 1, y + dir_y)]) {
    x -= 1;
    y += dir_y;
  }
  else if (!move_randomly && !dir_y && !grid[to_pos(x + dir_x, y + 1)]) {
    x += dir_x;
    y += 1;
  }
  else if (!move_randomly && !dir_y && !grid[to_pos(x + dir_x, y - 1)]) {
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

          if (!grid[to_pos(x + mv_x, y + mv_y)]) {
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
      } while (grid[to_pos(x + mv_x, y + mv_y)]);
      x += mv_x;
      y += mv_y;
    }
  }

  if (!found_direction)
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

void error(char* activity) {
  printf("%s failed: %s\n", activity, SDL_GetError());
  SDL_Quit();
  exit(-1);
}
