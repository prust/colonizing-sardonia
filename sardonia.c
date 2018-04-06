#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "SDL.h"

typedef unsigned char byte;

#define DELETED 0x1
#define BLOCK 0x2
#define BEAST 0x4
#define PLAYER 0x8
#define STATIC 0x10
#define TURRET 0x20
#define BORDER 0x40
#define SUPER 0x80

#define PROCESSED 0x1
#define ENCLOSED 0x2
#define PROCESSED_BORDER 0x4
#define POWERED 0x8

#define PICKING_UP 0
#define PLACING 1

short mode = PICKING_UP;
int num_collected_blocks = 0;
int block_ratio = 1; // you have to collect 2 rocks to build 1 wall
int num_blocks_per_turret = 6; // collect 6 rocks to build 1 turret
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

// forward-declare functions
int findAvailPos(entity* grid[]);
void move(entity* ent, entity* grid[], int x, int y);
void set_pos(entity* ent, entity* grid[], int pos);
void set_xy(entity* ent, entity* grid[], int x, int y);
void remove_from_grid(entity* ent, entity* grid[]);
int to_x(int ix);
int to_y(int ix);
int to_pos(int x, int y);
bool is_in_grid(int x, int y);
bool is_next_to_wall(entity* beast, entity* grid[], byte enclosures[]);
void beast_explode(entity* beast, entity* grid[], byte enclosures[]);
entity* closest_entity(int x, int y, entity entities[], int num_entities);
void del_entity(entity* ent, entity* grid[]);
int push(entity* grid[], int dir_x, int dir_y, int pos_x, int pos_y);
void checkForEnclosures(entity* grid[], byte enclosures[], int x, int y, bool new_check);
void toggleFullScreen(SDL_Window *win);
bool floodFill(entity* grid[], byte enclosures[], int prev_pos, int pos);
void floodClear(entity* grid[], byte enclosures[], int pos);
int imin(int i, int j);
bool inBounds(int x, int y);
double calc_dist(int x1, int y1, int x2, int y2);
void error(char* activity);

int block_w = 40;
int block_h = 40;
int bullet_w = 4;
int bullet_h = 4;
double bullet_speed = 300.0; // in px/sec
int block_density_pct = 20;
int starting_distance = 15;

const int num_players = 2;
entity players[num_players] = {{
  .flags = PLAYER,
  .x = 1,
  .y = 1
}, {
  .flags = (PLAYER | DELETED),
  .x = 2,
  .y = 2
}};

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
const int num_beasts = 10;
int max_turrets = 50;
int max_bullets = 100;

int main(int num_args, char* args[]) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    error("initializing SDL");

  srand(time(NULL));

  grid_len = num_blocks_w * num_blocks_h;
  entity* grid[grid_len];
  byte enclosures[grid_len];
  for (int i = 0; i < grid_len; ++i) {
    grid[i] = NULL;
    enclosures[i] = 0;
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
  set_pos(&players[0], grid, findAvailPos(grid));

  // additional 10 static blocks in the playing field
  for (int i = 0; i < 10; ++i) {
    int pos = findAvailPos(grid);
    static_blocks[ix].flags = (BLOCK | STATIC);
    static_blocks[ix].x = to_x(pos);
    static_blocks[ix].y = to_y(pos);
    grid[pos] = &static_blocks[ix];
    ix++;
  }

  // DRY violation: consolidate below into place_entities()
  int num_blocks = grid_len * block_density_pct / 100;
  entity blocks[num_blocks];
  for (int i = 0; i < num_blocks; ++i) {
    int pos = findAvailPos(grid);
    blocks[i].flags = BLOCK;
    blocks[i].x = to_x(pos);
    blocks[i].y = to_y(pos);
    grid[pos] = &blocks[i];
  }

  entity beasts[num_beasts];
  for (int i = 0; i < num_beasts; ++i) {
    int pos;
    do {
      pos = findAvailPos(grid);
    } while (abs(players[0].x - to_x(pos)) < starting_distance && abs(players[0].y - to_y(pos)) < starting_distance);

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
  // if (SDL_ShowCursor(SDL_DISABLE) < 0)
  //   error("hiding cursor");

  SDL_GetWindowSize(window, &vp.w, &vp.h);

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  bool is_gameover = false;
  int p1_dir_x = 0;
  int p1_dir_y = 0;
  int p2_dir_x = 0;
  int p2_dir_y = 0;
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
          p1_dir_x = 0;
          p1_dir_y = 0;
          p2_dir_x = 0;
          p2_dir_y = 0;
          switch (evt.key.keysym.sym) {
            case SDLK_ESCAPE:
              is_gameover = true;
              break;
            case SDLK_LEFT:
              p1_dir_x = -1;
              break;
            case SDLK_RIGHT:
              p1_dir_x = 1;
              break;
            case SDLK_UP:
              p1_dir_y = -1;
              break;
            case SDLK_DOWN:
              p1_dir_y = 1;
              break;
            case SDLK_a:
              p2_dir_x = -1;
              break;
            case SDLK_d:
              p2_dir_x = 1;
              break;
            case SDLK_s:
              p2_dir_y = 1;
              break;
            case SDLK_w:
              p2_dir_y = -1;
              break;
            case SDLK_MINUS:
              if ((evt.key.keysym.mod & KMOD_LSHIFT) || (evt.key.keysym.mod & KMOD_RSHIFT)) {
                block_w /= 2;
                block_h /= 2;
                vp.x = 0;
                vp.y = 0;
              }
              break;
            case SDLK_EQUALS:
              if ((evt.key.keysym.mod & KMOD_LSHIFT) || (evt.key.keysym.mod & KMOD_RSHIFT)) {
                block_w *= 2;
                block_h *= 2;
              }
              break;
            case SDLK_f:
              toggleFullScreen(window);
              break;
            case SDLK_2:
              // hitting "2" will toggle the 2nd player
              if (players[1].flags & DELETED)
                set_pos(&players[1], grid, findAvailPos(grid));
              else
                remove_from_grid(&players[1], grid);

              players[1].flags ^= DELETED; // toggle the bit
              break;
          }

          for (int i = 0; i < num_players; ++i) {
            int dir_x, dir_y;
            if (i == 0) {
              dir_x = p1_dir_x;
              dir_y = p1_dir_y;
            }
            else if (i == 1) {
              dir_x = p2_dir_x;
              dir_y = p2_dir_y;
            }

            if ((dir_x || dir_y) && !(players[i].flags & DELETED)) {
              int orig_x = players[i].x;
              int orig_y = players[i].y;
              int new_x = orig_x + dir_x;
              int new_y = orig_y + dir_y;
              entity* pushed_ent = NULL;
              if (new_x >= 0 && new_x < num_blocks_w &&
                  new_y >= 0 && new_y < num_blocks_h) {
                pushed_ent = grid[to_pos(new_x, new_y)];
              }

              int num_blocks_pushed = push(grid, dir_x, dir_y, players[i].x, players[i].y);
              if (num_blocks_pushed > -1) {
                move(&players[i], grid, new_x, new_y);
                if (num_blocks_pushed > 0 && pushed_ent) {
                  // check if previous enclosure was just broken
                  checkForEnclosures(grid, enclosures, pushed_ent->x - dir_x, pushed_ent->y - dir_y, false);
                  // check if we just expanded an enclosure
                  if (enclosures[to_pos(orig_x, orig_y)] & ENCLOSED)
                    enclosures[to_pos(new_x, new_y)] |= ENCLOSED;

                  int final_x = new_x;
                  int final_y = new_y;
                  if (dir_x)
                    final_x += num_blocks_pushed * dir_x;
                  else if (dir_y)
                    final_y += num_blocks_pushed * dir_y;

                  // check the last pushed block to see if a new enclosure was just created
                  checkForEnclosures(grid, enclosures, final_x, final_y, true);
                }
                
                if (is_spacebar_pressed) {
                  entity* ent_behind = grid[to_pos(orig_x - dir_x, orig_y - dir_y)];
                  if (ent_behind && (ent_behind->flags & BLOCK) && !(ent_behind->flags & STATIC)) {
                    move(ent_behind, grid, orig_x, orig_y);
                    checkForEnclosures(grid, enclosures, orig_x - dir_x, orig_y - dir_y, false);
                    checkForEnclosures(grid, enclosures, ent_behind->x, ent_behind->y, true);
                  }
                }
              }
            }
          }
        break;
      }
    }

    // shift the viewport if necessary, to include each player
    for (int i = 0; i < num_players; ++i) {
      if (players[i].flags & DELETED)
        continue;

      int player_x = players[i].x * block_w;
      int player_y = players[i].y * block_h;
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
    }

    // set BG color
    if (SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255) < 0)
      error("setting bg color");
    if (SDL_RenderClear(renderer) < 0)
      error("clearing renderer");

    for (int i = 0; i < num_blocks; ++i) {
      if (blocks[i].flags & DELETED)
        continue;

      if (blocks[i].flags & BORDER) {
        if (SDL_SetRenderDrawColor(renderer, 170, 160, 120, 255) < 0)
          error("setting enclosed border color");
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
    for (int i = 0; i < num_players; ++i) {
      if (players[i].flags & DELETED)
        continue;

      SDL_Rect player_rect = {
        .x = players[i].x * block_w - vp.x,
        .y = players[i].y * block_h - vp.y,
        .w = block_w,
        .h = block_h
      };
      if (SDL_RenderFillRect(renderer, &player_rect) < 0)
        error("filling rect");
    }

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

    // draw darkness
    for (int pos = 0; pos < grid_len; ++pos) {
      int x = to_x(pos);
      int y = to_y(pos);

      if ((enclosures[pos] & ENCLOSED) ||
        (inBounds(x + 1, y) && enclosures[to_pos(x + 1, y)] & ENCLOSED) ||
        (inBounds(x + 1, y + 1) && enclosures[to_pos(x + 1, y + 1)] & ENCLOSED) ||
        (inBounds(x, y + 1) && enclosures[to_pos(x, y + 1)] & ENCLOSED) ||
        (inBounds(x - 1, y) && enclosures[to_pos(x - 1, y)] & ENCLOSED) ||
        (inBounds(x - 1, y - 1) && enclosures[to_pos(x - 1, y - 1)] & ENCLOSED) ||
        (inBounds(x, y - 1) && enclosures[to_pos(x, y - 1)] & ENCLOSED) ||
        (inBounds(x + 1, y - 1) && enclosures[to_pos(x + 1, y - 1)] & ENCLOSED) ||
        (inBounds(x - 1, y + 1) && enclosures[to_pos(x - 1, y + 1)] & ENCLOSED))
        continue;

      double dist = -1;
      for (int i = 0; i < num_players; ++i) {
        if (players[i].flags & DELETED)
          continue;

        double player_dist = calc_dist(x, y, players[i].x, players[i].y);
        if (dist == -1 || player_dist < dist)
          dist = player_dist;
      }

      if (dist > 10) {
        SDL_Rect darkness = {
          .x = x * block_w - vp.x,
          .y = y * block_h - vp.y,
          .w = block_w,
          .h = block_h
        };

        if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 50) < 0)
          error("setting darkness color");
        if (SDL_RenderFillRect(renderer, &darkness) < 0)
          error("filling darkness rect");
      }
    }

    if (curr_time - last_fire_time >= turret_fire_interval) {
      for (int i = 0; i < max_turrets; ++i) {
        entity* turret = &turrets[i];
        if (turret->flags & DELETED)
          continue;

        // only turrets that are part of an enclosure can fire
        int pos = to_pos(turret->x, turret->y);
        if (!(turret->flags & BORDER))
          continue;

        entity* beast = closest_entity(turret->x, turret->y, beasts, num_beasts);
        if (beast) {
          double dist = calc_dist(beast->x, beast->y, turret->x, turret->y);
          // dividing by the distance gives us a normalized 1-unit vector
          double dx = (beast->x - turret->x) / dist;
          double dy = (beast->y - turret->y) / dist;
          for (int j = 0; j < max_bullets; ++j) {
            bullet* b = &bullets[j];
            if (b->flags & DELETED) {
              b->flags &= (~DELETED); // clear the DELETED bit

              // super turrets make super bullets
              if (turret->flags & SUPER)
                b->flags |= SUPER;
              
              // start in top/left corner
              int start_x = turret->x * block_w;
              int start_y = turret->y * block_h;
              if (dx > 0)
                start_x += block_w;
              else
                start_x -= 1; // so it's not on top of itself
              if (dy > 0)
                start_y += block_h;
              else
                start_y -= 1; // so it's not on top of itself

              b->x = start_x;
              b->y = start_y;
              b->dx = dx;
              b->dy = dy;
              break;
            }
          }
        }
        // TODO: determine when max_bullets is exceeded & notify player?
      }
      last_fire_time = curr_time;
    }

    if (curr_time - last_move_time >= beast_speed) {
      for (int i = 0; i < num_beasts; ++i) {
        if (beasts[i].flags & DELETED)
          continue;

        if (is_next_to_wall(&beasts[i], grid, enclosures)) {
          if (beasts[i].flags & SUPER || rand() % 100 >= 98) {
            beast_explode(&beasts[i], grid, enclosures);
            continue;
          }
        }

        int x = beasts[i].x;
        int y = beasts[i].y;

        int dir_x = 0;
        int dir_y = 0;
        if (players[0].x < x)
          dir_x = -1;
        else if (players[0].x > x)
          dir_x = 1;

        if (players[0].y < y)
          dir_y = -1;
        else if (players[0].y > y)
          dir_y = 1;

        bool found_direction = true;

        // a quarter of the time we want them to move randomly
        // this keeps them from being too deterministic & from getting stuck
        // behind walls, etc
        bool move_randomly = rand() % 100 > 75;

        // if the beast isn't within the attack distance, it should move randomly
        double dist = calc_dist(players[0].x, players[0].y, beasts[i].x, beasts[i].y);
        if (dist > attack_dist)
          move_randomly = true;

        // the beast will "get" the players[0] on this move
        if (abs(players[0].x - x) <= 1 && abs(players[0].y - y) <= 1) {
          is_gameover = true;
          x = players[0].x;
          y = players[0].y;
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

        // if the beast is surrounded by blocks & has nowhere to move, it blows up
        if (!found_direction)
          beast_explode(&beasts[i], grid, enclosures);
        else
          move(&beasts[i], grid, x, y);
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

bool is_next_to_wall(entity* beast, entity* grid[], byte enclosures[]) {
  for (int dir_x = -1; dir_x <= 1; ++dir_x) {
    for (int dir_y = -1; dir_y <= 1; ++dir_y) {
      // check the bounds
      int new_x = beast->x + dir_x;
      int new_y = beast->y + dir_y;
      if (!is_in_grid(new_x, new_y))
        continue;

      int pos = to_pos(new_x, new_y);
      if (grid[pos] && grid[pos]->flags & BORDER)
        return true;
    }
  }
  return false;
}

void beast_explode(entity* beast, entity* grid[], byte enclosures[]) {
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

  // BUG: this only works if the beast was IN the enclosure
  // not if the beast outside the wall of an enclosure
  // we can check `enclosures` to know which situation we're in (if either)...

  // check if previous enclosure was just broken
  checkForEnclosures(grid, enclosures, x, y, false);
}

void del_entity(entity* ent, entity* grid[]) {
  ent->flags |= DELETED; // flip DELETED bit on
  ent->flags &= (~BORDER); // clear BORDER flag since the block will be re-used
  remove_from_grid(ent, grid);
}

int push(entity* grid[], int dir_x, int dir_y, int pos_x, int pos_y) {
  entity* first_ent = grid[to_pos(pos_x + dir_x, pos_y + dir_y)];
  if (!first_ent)
    return 0;
  if (first_ent->flags & STATIC)
    return -1;

  int second_x = pos_x + dir_x*2;
  int second_y = pos_y + dir_y*2;
  int num_blocks_pushed;
  entity* second_ent = grid[to_pos(second_x, second_y)];
  if (!second_ent) {
    num_blocks_pushed = 1;
  }
  else if (second_ent->flags & BLOCK) {
    num_blocks_pushed = push(grid, dir_x, dir_y, pos_x + dir_x, pos_y + dir_y);
    if (num_blocks_pushed > -1)
      num_blocks_pushed++;
  }
  else if (second_ent->flags & PLAYER) {
    num_blocks_pushed = -1;
  }
  else if (second_ent->flags & BEAST) {
    // if there's a block on the other side, squish beast between blocks
    entity* third_ent = grid[to_pos(pos_x + dir_x*3, pos_y + dir_y*3)];
    if (third_ent && third_ent->flags & BLOCK) {
      del_entity(second_ent, grid);
      num_blocks_pushed = 0;
    }
    else {
      num_blocks_pushed = -1;
    }
  }

  if (num_blocks_pushed > -1)
    move(first_ent, grid, second_x, second_y);

  return num_blocks_pushed;
}

int findAvailPos(entity* grid[]) {
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

// check all sides of the last pushed block for newly-created enclosures
void checkForEnclosures(entity* grid[], byte enclosures[], int x, int y, bool new_check) {
  if (!is_in_grid(x, y))
    return;

  // clear the PROCESSED & PROCESSED_BORDER bits from everything before beginning
  for (int i = 0; i < grid_len; ++i) {
    enclosures[i] &= (~PROCESSED);
    enclosures[i] &= (~PROCESSED_BORDER);
    enclosures[i] &= (~POWERED);
  }

  for (int dir_x = -1; dir_x <= 1; ++dir_x) {
    for (int dir_y = -1; dir_y <= 1; ++dir_y) {
      // check the bounds
      int new_x = x + dir_x;
      int new_y = y + dir_y;
      if (!is_in_grid(new_x, new_y))
        continue;

      int pos = to_pos(new_x, new_y);
      
      // if it's a block, skip it (only flood-fill non-blocks)
      if (grid[pos] && grid[pos]->flags & BLOCK)
        continue;

      // if we processed this square in the last flood-fill, skip it
      if (enclosures[pos] & PROCESSED)
        continue;

      // clear the processed bits from the last flood-fill
      for (int i = 0; i < grid_len; ++i) {
        enclosures[i] &= (~PROCESSED);
        enclosures[i] &= (~PROCESSED_BORDER);
        enclosures[i] &= (~POWERED);
      }
      
      // everything that can flood-fill is enclosed; mark it as such
      int prev_pos = -1;
      bool is_enclosure = floodFill(grid, enclosures, prev_pos, pos);

      if (new_check && is_enclosure) {
        int size = 0;
        bool is_powered = false;
        for (int i = 0; i < grid_len; ++i) {
          if (enclosures[i] & PROCESSED) {
            enclosures[i] |= ENCLOSED;
            size++;
          }
          if ((enclosures[i] & PROCESSED_BORDER) && grid[i])
            grid[i]->flags |= BORDER;
          if (enclosures[i] & POWERED)
            is_powered = true;
        }

        // TODO: assign fortress FKs and set fortress.size

        if (is_powered)
          for (int i = 0; i < grid_len; ++i)
            if ((enclosures[i] & PROCESSED_BORDER) && grid[i])
              grid[i]->flags |= SUPER; // super block = power stone
      }
      else if (!new_check && !is_enclosure && enclosures[pos] & ENCLOSED) {
        floodClear(grid, enclosures, pos);
      }
    }
  }
}

// instead of walking paths & borders, do a flood-fill
// disallow edge-blocks, if you touch an edge (define by x/y, not static), then it's not enclosed
// if an edge is not touched, then hurrah, we have an enclosed area
// we should be able to have a recursive tree of function calls, if any hit an edge, they return false
// at the end, we have a true/false result -- at that point, we can either walk them all & flip the ENCLOSED bit or unflip it
bool floodFill(entity* grid[], byte enclosures[], int prev_pos, int pos) {
  int x = to_x(pos);
  int y = to_y(pos);
  
  // we've branched off & hit something already covered by another branch
  if (enclosures[pos] & PROCESSED)
    return true;

  // mark it as processed
  enclosures[pos] |= PROCESSED;

  for (int dir_x = -1; dir_x <= 1; ++dir_x) {
    for (int dir_y = -1; dir_y <= 1; ++dir_y) {
      // disallow no movement
      if (!dir_x && !dir_y)
        continue;

      // check the bounds
      int new_x = x + dir_x;
      int new_y = y + dir_y;
      if (!is_in_grid(new_x, new_y))
        continue;

      // ignore the previous position
      int next_pos = to_pos(new_x, new_y);
      if (next_pos == prev_pos)
        continue;

      // if we hit one of the edges, then this is *not* enclosed
      if (new_x == 0 || new_y == 0 || new_x == num_blocks_w - 1 || new_y == num_blocks_h - 1)
        return false;

      // mark border of processed area
      if (grid[next_pos] && grid[next_pos]->flags & BLOCK) {
        enclosures[next_pos] |= PROCESSED_BORDER;
        if (grid[next_pos]->flags & STATIC)
          enclosures[next_pos] |= POWERED;
      }

      if (!grid[next_pos] || !(grid[next_pos]->flags & BLOCK))
        if (!floodFill(grid, enclosures, pos, next_pos))
          return false;
    }
  }
  return true;
}

void floodClear(entity* grid[], byte enclosures[], int pos) {
  if (!(enclosures[pos] & ENCLOSED))
    return;

  // clear the ENCLOSURE bit
  enclosures[pos] &= (~ENCLOSED);

  int x = to_x(pos);
  int y = to_y(pos);
  for (int dir_x = -1; dir_x <= 1; ++dir_x) {
    for (int dir_y = -1; dir_y <= 1; ++dir_y) {
      // disallow no movement
      if (!dir_x && !dir_y)
        continue;

      // check the bounds
      int new_x = x + dir_x;
      int new_y = y + dir_y;
      if ((new_x < 0 || new_x >= num_blocks_w) ||
        (new_y < 0 || new_y >= num_blocks_h))
          continue;

      int next_pos = to_pos(new_x, new_y);
      
      // clear the BORDER bit
      // BUG: if a block was bordering TWO enclosed areas & one cleared,
      // this will make the game will think it is not an enclosed border
      if (grid[next_pos] && grid[next_pos]->flags & BLOCK)
        grid[next_pos]->flags &= (~BORDER);

      if (!grid[next_pos] || !(grid[next_pos]->flags & BLOCK))
        floodClear(grid, enclosures, next_pos);
    }
  }
}

void toggleFullScreen(SDL_Window *win) {
  Uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) || (flags & SDL_WINDOW_FULLSCREEN))
    flags = 0;
  else
    flags = SDL_WINDOW_FULLSCREEN;

  if (SDL_SetWindowFullscreen(win, flags) < 0)
    error("Toggling fullscreen mode failed");
}

int imin(int i, int j) {
  if (i < j)
    return i;
  else
    return j;
}

bool inBounds(int x, int y) {
  return x >= 0 && x < num_blocks_w &&
    y >= 0 && y < num_blocks_h;
}

double calc_dist(int x1, int y1, int x2, int y2) {
  return sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2));
}

void error(char* activity) {
  printf("%s failed: %s\n", activity, SDL_GetError());
  SDL_Quit();
  exit(-1);
}
