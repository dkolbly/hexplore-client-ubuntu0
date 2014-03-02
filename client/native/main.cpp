#include <unistd.h>
#include <sys/types.h>
#include "ui.h"
#include <zlib.h>
#include <sys/time.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <unistd.h>
#include "connection.h"
#include <hexcom/hex.h>
#include <hexcom/picture.h>
#include "text_ui.h"
#include "wirehandler.h"
#include "wire/terrain.pb.h"
#include "wire/hello.pb.h"
#include "wire/entity.pb.h"
#include "entity.h"
#include "sound.h"
// SDL2-ttf is not available in 13.10 (!)
#if HAVE_SDL2_TTF
#include <SDL_ttf.h>
#endif

#include <hexcom/curve.h>
#include "skymap.h"
#include "clientoptions.h"

/*
see http://www.rioki.org/2013/03/07/glsl-skybox.html
*/
GLuint cube_map_texture_id;


struct {
  char msg1[100];
  bool show_adj;
  Posn adj;
} status;

#define STATUS_WINDOW_WIDTH     (640)
#define STATUS_WINDOW_HEIGHT     (640)

enum StandardTools {
  TOOL_BUILDER,
  TOOL_DESTROYER,
  TOOL_ENTITY_SELECTER,
  TOOL_MAGNIFY,
  TOOL_BINOCULAR
};

FILE *debug_animus = NULL;
unsigned nextAnimusUid = 1;

void ui_hopup(UserInterface *ui, float dz, float duration);
bool ui_remove_animus(UserInterface *ui, PlayerAnimus *a);

static Curve *hop_up;
#define RUN_SPEED_FACTOR                (2.0f)
#define RAILWAY_WALK_SPEED_FACTOR       (3.0f)
#define CROUCH_WALK_SPEED_FACTOR        (0.5f)
#define SELECTION_RANGE                 (12.0)

void show_axes(UserInterface *ui, glm::mat4 model);
void show_box(UserInterface *ui, frect box);

struct Posture {
  uint8_t       clearance;
  uint8_t       ceiling;
  float         eye;
};

#define STANDING_POSTURE                (0)
#define CROUCHING_POSTURE               (1)
#define CRAWLING_POSTURE                (2)

static const Posture posture_info[3] = { 
  { .clearance = 15, .ceiling = 19, .eye = 18*z_scale },
  { .clearance = 9, .ceiling = 11, .eye = 13*z_scale },
  { .clearance = 6, .ceiling = 7,  .eye = 6*z_scale }
};

#define STANDING_EYE_HEIGHT             (18 * z_scale)
#define STANDING_CEILING_HEIGHT         (19)
#define CROUCHING_EYE_HEIGHT            (9 * z_scale)
#define CROUCHING_CEILING_HEIGHT        (11)
#define CROUCH_SPEED                    (4.0)
#define STANDUP_SPEED                   (CROUCH_SPEED*1.5)

#define TEXT_POPUP_W  (8*80)
#define TEXT_POPUP_H  (8*2)

unsigned keymap[] = {
  SDL_SCANCODE_W,
  SDL_SCANCODE_S,
  SDL_SCANCODE_A,
  SDL_SCANCODE_D,
  SDL_SCANCODE_C,       // crouch
  SDL_SCANCODE_R,       // run
  SDL_SCANCODE_SPACE    // jump
};

// these are also the bit numbers in ui->activePlayerAnimae
#define WALK_FORWARD    (0)
#define WALK_BACKWARD   (1)
#define SLIDE_LEFT      (2)
#define SLIDE_RIGHT     (3)
#define CROUCH          (4)
#define RUN             (5)
#define JUMP            (6)

#define HOP             (20)

#define ANY_WALKING_MASK ((1<<WALK_FORWARD)|(1<<WALK_BACKWARD)|(1<<SLIDE_LEFT)|(1<<SLIDE_RIGHT)|(1<<RUN))

#define NUMVERTICES     (6*2)               // 12

#define NUMTRI (4+4 + 2+2+2+2+2+2)      // 20

void fatal_error(const char *msg, const char *details, 
                 const char *file, int line)
{
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, 
               "%s:%d: Fatal error: %s\ndetails: %s\n", 
               file, line, msg,
               details);
  SDL_Quit();
  exit(9);
}

#define fatal(msg, details) fatal_error(msg, details, __FILE__, __LINE__)

void ui_inputbox_init(UserInterface *ui)
{
  ui->inputbox.popupTime = 0;
  ui->inputbox.dirty = false;
  ui->inputbox.text = "";
}

void ui_inputbox_start_typing(UserInterface *ui)
{
  ui->inputbox.popupTime = ui->frameTime;
  ui->inputbox.cursor = ui->inputbox.text.size();
  ui->inputbox.dirty = true;
}

int ui_draw_overlay(UserInterface *ui, OverlayWindow *overlay,
                    float dx, float dy)
{
  if (!overlay) {
    return 0;
  }
  if (overlay->ow_showtime == 0) {
    return 0;
  }
  if (overlay->ow_cleartime < ui->frameTime) {
    overlay->ow_showtime = 0;
    return 0;
  }

  glUseProgram(0);
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  float texw, texh;
  SDL_GL_BindTexture(overlay->ow_content, &texw, &texh);

  struct { double x, y, z; } vertex_coords[6];
  struct { double u, v; } texture_coords[6];

  /*int w, h;
    SDL_GetWindowSize(ui->window, &w, &h);
  printf("window size %d, %d\n", w, h);*/

  double x0 = overlay->ow_x0 + dx;
  double y0 = overlay->ow_y0 + dy;
  double x1 = overlay->ow_x1 + dx;
  double y1 = overlay->ow_y1 + dy;

  vertex_coords[0].x = x0;
  vertex_coords[0].y = y1;
  vertex_coords[0].z = 0;
  texture_coords[0].u = 0;
  texture_coords[0].v = 0;

  vertex_coords[1].x = x1;
  vertex_coords[1].y = y0;
  vertex_coords[1].z = 0;
  texture_coords[1].u = texw;
  texture_coords[1].v = texh;

  vertex_coords[2].x = x0;
  vertex_coords[2].y = y0;
  vertex_coords[2].z = 0;
  texture_coords[2].u = 0;
  texture_coords[2].v = texh;

  vertex_coords[3].x = x0;
  vertex_coords[3].y = y1;
  vertex_coords[3].z = 0;
  texture_coords[3].u = 0;
  texture_coords[3].v = 0;

  vertex_coords[4].x = x1;
  vertex_coords[4].y = y1;
  vertex_coords[4].z = 0;
  texture_coords[4].u = texw;
  texture_coords[4].v = 0;

  vertex_coords[5].x = x1;
  vertex_coords[5].y = y0;
  vertex_coords[5].z = 0;
  texture_coords[5].u = texw;
  texture_coords[5].v = texh;
  
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_VERTEX_ARRAY);
  //glEnableClientState(GL_COLOR_ARRAY);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glVertexPointer(3, GL_DOUBLE, 0, &vertex_coords[0]);
  glTexCoordPointer(2, GL_DOUBLE, 0, &texture_coords[0]);
  //glColorPointer(4, GL_FLOAT, 0, &color[0]);

  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  unsigned indices[6] = { 0, 1, 2, 3, 4, 5};
  glColor4f(1, 1, 1, 1);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indices[0]);

  //glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  SDL_GL_UnbindTexture(overlay->ow_content);

  return 1;
}

void ui_popup_draw(UserInterface *ui)
{
  if (ui->inputbox.popupTime == 0) {
    return;
  }

  glUseProgram(0);
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  //glColor4f(0.5, 0.5, 0, 0.5);
  //glRectf(-0.75f,-0.8f, 0.75f, -0.7f);

  //SDL_GL_MakeCurrent(ui->window, ui->glcontext);
  //SDL_RenderCopy(ui->renderer, ui->popupTexture, NULL, NULL);

  int window_w, window_h;
  SDL_GetWindowSize(ui->window, &window_w, &window_h);

  float texw, texh;
  SDL_GL_BindTexture(ui->popupTexture, &texw, &texh);

  struct { double x, y, z; } vertex_coords[6];
  struct { double u, v; } texture_coords[6];

  double screen_horz_rez = 2.0 / window_w;
  double screen_vert_rez = 2.0 / window_h;

  double x0 = -0.75f;
  double y0 = -0.8f;
  double x1 = x0 + screen_horz_rez * TEXT_POPUP_W * 2;
  double y1 = y0 + screen_vert_rez * TEXT_POPUP_H * 2;

  vertex_coords[0].x = x0;
  vertex_coords[0].y = y1;
  vertex_coords[0].z = 0;
  texture_coords[0].u = 0;
  texture_coords[0].v = 0;

  vertex_coords[1].x = x1;
  vertex_coords[1].y = y0;
  vertex_coords[1].z = 0;
  texture_coords[1].u = texw;
  texture_coords[1].v = texh;

  vertex_coords[2].x = x0;
  vertex_coords[2].y = y0;
  vertex_coords[2].z = 0;
  texture_coords[2].u = 0;
  texture_coords[2].v = texh;

  vertex_coords[3].x = x0;
  vertex_coords[3].y = y1;
  vertex_coords[3].z = 0;
  texture_coords[3].u = 0;
  texture_coords[3].v = 0;

  vertex_coords[4].x = x1;
  vertex_coords[4].y = y1;
  vertex_coords[4].z = 0;
  texture_coords[4].u = texw;
  texture_coords[4].v = 0;

  vertex_coords[5].x = x1;
  vertex_coords[5].y = y0;
  vertex_coords[5].z = 0;
  texture_coords[5].u = texw;
  texture_coords[5].v = texh;
  
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_VERTEX_ARRAY);
  //glEnableClientState(GL_COLOR_ARRAY);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glVertexPointer(3, GL_DOUBLE, 0, &vertex_coords[0]);
  glTexCoordPointer(2, GL_DOUBLE, 0, &texture_coords[0]);
  //glColorPointer(4, GL_FLOAT, 0, &color[0]);

  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  unsigned indices[6] = { 0, 1, 2, 3, 4, 5};
  glColor4f(1, 1, 1, 1);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indices[0]);
  //glColor4f(1, 1, 1, 1);
  //glRectf(-0.75f,-0.6f, 0.75f, -0.5f);

  //glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  SDL_GL_UnbindTexture(ui->popupTexture);
}

OverlayWindow *ui_load_overlay(UserInterface *ui, 
                               const char *path,
                               float x0, float y0)
{
  Picture *p = Picture::load_png(path);
  OverlayWindow *w = new OverlayWindow();

  int window_w, window_h;
  SDL_GetWindowSize(ui->window, &window_w, &window_h);
  printf("window size %d, %d\n", window_w, window_h);

  w->ow_x0 = x0;
  w->ow_y0 = y0 - p->height * (2.0/window_h);
  w->ow_x1 = x0 + p->width * (2.0/window_w);
  w->ow_y1 = y0;
  w->ow_showtime = 0;

  w->ow_content = SDL_CreateTexture(ui->renderer,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    p->width, 
                                    p->height);
  SDL_SetTextureBlendMode(w->ow_content, SDL_BLENDMODE_BLEND);

  Uint32 *dst;
  void *pixels;
  int pitch;

  if (SDL_LockTexture(w->ow_content, NULL, &pixels, &pitch) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, 
                 "Couldn't lock texture: %s\n", SDL_GetError());
    return NULL;
  }
  uint16_t *rp = p->red_plane();
  uint16_t *gp = p->green_plane();
  uint16_t *bp = p->blue_plane();
  uint16_t *ap = p->alpha_plane();
  unsigned pic_height = p->height;
  unsigned pic_width = p->width;

  for (unsigned row = 0; row < pic_height; ++row) {
    dst = (Uint32*)((Uint8*)pixels + row * pitch);
    for (unsigned col = 0; col < pic_width; ++col) {
      int r, g, b, a;
      r = rp[row*pic_width+col] >> 8;
      g = gp[row*pic_width+col] >> 8;
      b = bp[row*pic_width+col] >> 8;
      a = ap[row*pic_width+col] >> 8;
      *dst++ = (a << 24)|(r<<16)|(g<<8)|(b);
    }
  }
  SDL_UnlockTexture(w->ow_content);
  return w;
}

struct LockedTexture {
  SDL_Texture *tex;
  void *pixels;
  int pitch;
  unsigned pitch_words;
  unsigned width;
  unsigned height;
  LockedTexture(SDL_Texture *texture, unsigned w, unsigned h) 
    : tex(NULL),
      pixels(NULL),
      width(w),
      height(h)
  {
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Couldn't lock texture: %s\n", 
                   SDL_GetError());
      return;
    }
    pitch_words = pitch / sizeof(uint32_t);
    /*printf("Locked texture at %p for %u x %u ; pitch %d\n", 
           pixels,
           width, height,
           pitch);*/
    tex = texture;
  }
  bool valid() {
    return (pixels != NULL);
  }
          
  ~LockedTexture() {
    if (tex) {
      SDL_UnlockTexture(tex);
    }
  }

  // color format is SDL_PIXELFORMAT_ARGB8888
  void put_pixel(unsigned x, unsigned y, uint32_t color) const {
    if ((x >= width) || (y >= width)) {
      // clip outside the texture
      return;
    }
    size_t k = x + y * pitch_words;
    ((uint32_t*)pixels)[k] = color;
  }

};

int ui_drawtext(UserInterface *ui,
                LockedTexture const& tex,
                int x, int y,
                std::string const& str)
{
  Picture *font = ui->popupFont;
  uint16_t *red = font->red_plane();
  unsigned stride = font->width;

  for (size_t i=0; i<str.size(); i++) {
    uint8_t ch = str[i];
    for (int dy=0; dy<8; dy++) {
      for (int dx=0; dx<8; dx++) {
        if (red[dx + ch * 8 + dy * stride] < 0x8000) {
          tex.put_pixel(x + dx + i*8,
                        y + dy,
                        0xFF000000);
        }
      }
    }
  }
  return 0;
}


void ui_popup_update(UserInterface *ui)
{
  if (!ui->inputbox.dirty) {
    return;
  }
  ui->inputbox.dirty = false;

  SDL_Texture *texture = ui->popupTexture;
  Uint32 *dst;
  int row, col;
  void *pixels;
  int pitch;

  LockedTexture ltex(texture, TEXT_POPUP_W, TEXT_POPUP_H);

  if (SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0) {
    return;
  }
  for (row = 0; row < TEXT_POPUP_H; ++row) {
    dst = (Uint32*)((Uint8*)pixels + row * pitch);
    for (col = 0; col < TEXT_POPUP_W; ++col) {
      int r, g, b;
      r = (sin(row/10.0)*100)+110;
      g = r;
      b = r;
      int a = r;
      *dst++ = (a << 24)|(r<<16)|(g<<8)|(b);
    }
  }

  ui_drawtext(ui, ltex, 0, 0, ui->inputbox.text);
  
}

Animus::~Animus()
{
}

bool PlayerCrouchAnimus::update(double dt)
{
  float goal = pca_crouchingDown ? CROUCHING_EYE_HEIGHT : STANDING_EYE_HEIGHT;
  pa_ui->eyeHeight = dt * (goal - pca_initialEyeHeight) + pca_initialEyeHeight;
  printf("PCA eyeHeight %.3f\n", pa_ui->eyeHeight);
  return true;
}

void PlayerCrouchAnimus::complete()
{
  pa_ui->eyeHeight = pca_crouchingDown 
    ? CROUCHING_EYE_HEIGHT 
    : STANDING_EYE_HEIGHT;
}

void PlayerJumpAnimus::complete()
{
  //printf("jump %p complete (%#x)\n", this, pa_keyFlags);

  SpanInfo s = pa_ui->world->getSpanBelow(pa_ui->location);
  if (false && s.span) {
    pa_ui->location[2] = z_scale * (s.z + s.span->height);
    if (debug_animus) {
      fprintf(debug_animus, "%.6f complete_jump %u altitude %.4f span {%d,%d}\n", 
              (pa_ui->frameTime - pa_ui->launchTime) * 1e-6,
              a_uid,
              pa_ui->location[2],
              s.x, s.y);
    }
  } else {
    pa_ui->location[2] = pja_finalAltitude;
    if (debug_animus) {
      fprintf(debug_animus, "%.6f complete_jump %u altitude %.4f nospan\n",
              (pa_ui->frameTime - pa_ui->launchTime) * 1e-6,
              a_uid,
              pa_ui->location[2]);
    }
  }
  pa_ui->activePlayerAnimae &= ~((1<<JUMP)|(1<<HOP));
}

bool PlayerJumpAnimus::update(double dt)
{
  double base = dt * (pja_finalAltitude - pja_initialAltitude) + pja_initialAltitude;
  double x = dt - 0.5;
  double altitude = base + 4.0 * (0.25 - x * x) * pja_jumpHeight;
  pa_ui->location[2] = altitude;
  if (debug_animus) {
    fprintf(debug_animus, "%.6f jump %u altitude %.4f\n",
            (pa_ui->frameTime - pa_ui->launchTime)*1e-6,
            a_uid,
            altitude);
  }

  return true;
}

/**
 *  Update the underlying entity's attributes according to the 
 *  current time relative to the animation, which is between 0.0 and 1.0
 */

bool PlayerWalkingAnimus::update(double dt)
{
  double ha = DEG_TO_RAD(pwa_direction);
  glm::vec3 direction(cos(ha), sin(ha), 0);

  glm::vec3 changeloc = direction * float(dt * pwa_speed);
  // walking only changes (X,Y) not Z
  pa_ui->location[0] = pwa_startloc[0] + changeloc[0];
  pa_ui->location[1] = pwa_startloc[1] + changeloc[1];

  // check to see if there's a change in altitude to deal with

  static const bool verbose = false;

  SpanInfo s = pa_ui->world->getSpanBelow(pa_ui->location);
  {
    Posn posn(s.x & ~(REGION_SIZE-1), s.y & ~(REGION_SIZE-1));
    if (!(posn == pa_ui->currentRegionPosn)) {
      // check to see if we have this and all the adjacent regions cached
      for (int drx=-1; drx<=1; drx++) {
        for (int dry=-1; dry<=1; dry++) {
          Posn p(posn.x + drx * REGION_SIZE, posn.y + dry * REGION_SIZE);
          pa_ui->world->requestRegionIfNotPresent(pa_ui->cnx, p);
        }
      }
    }
  }
  if (!s.span) {
    return false;
  }

  if (verbose) {
    printf("   over span: %p [%d..%d]\n", 
           s.span, s.z, s.z + s.span->height);
  }
  
  pa_ui->spanOnTopOf = *s.span;
  // how far are we from the center of our current hex?
  double dx = hex_center_x(s.x, s.y) - pa_ui->location[0];
  double dy = hex_center_y(s.x, s.y) - pa_ui->location[1];
  double r1 = sqrt(dx*dx + dy*dy);
  if (verbose) {
    printf("   from center %.3f\n", r1);
  }

  if (debug_animus) {
    fprintf(debug_animus, "%.6f walking %u %.4f : to %.4f %.4f range %.4f",
            (pa_ui->frameTime - pa_ui->launchTime) * 1e-6,
            a_uid,
            dt,
            pa_ui->location[0], pa_ui->location[1], r1);
  }
  status.show_adj = false;
  sprintf(status.msg1, "range from {%d,%d} %.3f", s.x, s.y, r1);

  if (r1 < 0.8/2.0) {
    // we're still pretty close to the center of this one; don't worry
    // about it yet
    if (debug_animus) {
      fprintf(debug_animus, " inhex\n");
    }
    return true;
  }
  double facing_dot_dx = direction[0] * dx + direction[1] * dy;
  sprintf(status.msg1 + strlen(status.msg1), " f.dx=%.3f", facing_dot_dx);
  if (facing_dot_dx > 0) {
    // we are headed towards the center... nothing to worry about
    if (debug_animus) {
      fprintf(debug_animus, " towardscenter\n");
    }
    return true;
  }

  ColumnPick cp;
  int rc = hexPickColumn(pa_ui->location, direction, s.x, s.y, &cp);
  assert(rc);
  sprintf(status.msg1 + strlen(status.msg1), " Xf %d", cp.exit_face);
  if (debug_animus) {
    fprintf(debug_animus, " Xf %d", cp.exit_face);
  }

  if (cp.exit_range > 0.2) {
    // we aren't leaving this hex just yet
    if (debug_animus) {
      fprintf(debug_animus, " notleaving %.3f\n", cp.exit_range);
    }
    return true;
  }
    
  SpanInfo adj = pa_ui->world->getSpanAdjacentByFace(s, cp.exit_face);
  // TODO, just in case we can't find a span, we should do something about
  // it, like either fall into the infinite depth or abort the move

  status.show_adj = true;
  status.adj = Posn(adj.x, adj.y);
  if (!adj.span) {
    if (debug_animus) {
      fprintf(debug_animus, " nospan\n");
    }
    return false;
  }

  sprintf(status.msg1 + strlen(status.msg1), " adj {%d,%d}", adj.x, adj.y);

  if (verbose) {
    printf("   forward => (%d,%d) [%d..%d]\n",
           adj.x, adj.y,
           adj.z, adj.z + adj.span->height);
  }
  assert(adj.region);
  Region *rgn = adj.region;
  int idy = adj.y - rgn->origin.y;
  int idx = adj.x - rgn->origin.x;
  if (debug_animus) {
    fprintf(debug_animus, " adj {%d,%d}", adj.x, adj.y);
  }
  int z = floor(pa_ui->location[2] / z_scale);
  SpanVector const& spanvec(rgn->columns[idy][idx]);

  unsigned char v_type[64];
  const int floor_index = 32;
  expandSpan(rgn, spanvec, z-floor_index, sizeof(v_type), &v_type[0], NULL);

#define MAX_STEP_UP     (5)
#define MAX_JUMP_UP     (20)
#define MAX_STEP_DOWN   (-3)

  const Posture *use_posture = &posture_info[STANDING_POSTURE];
  if (pa_ui->activePlayerAnimae & (1<<CROUCH)) {
    use_posture = &posture_info[CROUCHING_POSTURE];
  }
  // We can step up by a little bit
  int max_step_up = MAX_STEP_UP;
  if (pa_ui->activePlayerAnimae & (1<<JUMP)) {
    max_step_up = MAX_JUMP_UP;
  }

  int stepping_up = 0;
  while (v_type[floor_index+stepping_up]
         && (stepping_up < max_step_up)) {
    stepping_up++;
  }
  if (v_type[floor_index+stepping_up]) {
    printf("too far -- step up is more than %d\n", max_step_up);
    if (debug_animus) {
      fprintf(debug_animus, " bigstepup %d\n", max_step_up);
    }
    return false;
  }

#if 0
  printf("  <--------------------------------v-------------------------------> z=%d\n", z);
  printf("   ");
  for (int i=0; i<64; i++) {
    if (v_type[i]) {
      printf("*");
    } else {
      printf(" ");
    }
  }
  printf("\n");
  printf("   ");
  for (int i=0; i<64; i++) {
    if (i == (floor_index + stepping_up)) {
      printf("^");
    } else {
      printf(" ");
    }
  }
  printf("\n");
#endif


  // There must be at least a certain amount of clearance above our
  // current level before we can go into the gap

  for (int i=0; i<use_posture->clearance; i++) {
    if (v_type[i + floor_index + stepping_up]) {
      printf("bonked head stepping up %d!\n", stepping_up);
      if (debug_animus) {
        fprintf(debug_animus, " tight %d\n", stepping_up);
      }
      return false;
    }
  }

  // and it has to have a certain ceiling
#if 0
  for (int i=0; i<use_posture->ceiling; i++) {
    if (v_type[floor_index + stepping_up + use_posture->clearance - i]) {
      printf("ceiling too low (need ceiling %d, found %d+%d+%d)\n", use_posture->ceiling, i);
      if (debug_animus) {
        fprintf(debug_animus, " bonk %d\n", stepping_up);
      }
      return false;
    }
  }
#endif

  // and if the floor drops off, we have to step down regardless

  //dx = hex_center_x(adj.x, adj.y) - ui->location[0];
  //dy = hex_center_y(adj.x, adj.y) - ui->location[1];
  //double r2 = sqrt(dx*dx + dy*dy);
  //double zbump1 = (s.z + s.span->height) * (r2 / (r1+r2));
  //double zbump2 = (adj.z + adj.span->height) * (r1 / (r1+r2));
  int newzi = (s.z + s.span->height);
  int adjzi = (adj.z + adj.span->height);
  //float newz = (s.z + s.span->height) * z_scale;

  /*
  if ((adjzi-newzi) > 5) {
    // yikes, this is too low of a ceiling
    // TODO: 5 is too short, and need to account for CROUCH also
    printf("trying to go from (%d,%d,%d) to (%d,%d,%d) dir %.3f\n",
           s.x, s.y, s.z + s.span->height,
           adj.x, adj.y, adj.z + adj.span->height,
           pwa_direction);
    printf("denied: z=%d adjz=%d (step up too high)\n", 
           newzi, adjzi);
    return false;
  }*/

  bool didhop = false;

  if ((adjzi != newzi) && !(pa_ui->activePlayerAnimae & (1<<HOP))) {
    // how far we have to go until we get to the center of the adj hex
    float delta_r = x_stride - r1;
    if (delta_r > 0) {
      // how long will it take to get there
      float delta_t = delta_r / pwa_speed;
      //printf("hop up needs to cover dr %.3f in dt %g\n", delta_r, delta_t);
      pa_ui->activePlayerAnimae |= (1<<HOP);
      float dz = (adjzi * z_scale) - pa_ui->location[2];
      if (debug_animus) {
        fprintf(debug_animus, " hopup %.3f\n", dz);
        didhop = true;
      }
      ui_hopup(pa_ui, dz, delta_t);
    }
  }
  if (debug_animus && !didhop) {
    fprintf(debug_animus, "\n");
  }
  return true;
}

void ui_update_viewpoint(UserInterface *ui)
{
  if (debug_animus) {
    fprintf(debug_animus, "%.6f viewpoint %.4f %.4f %.4f + %.4f   %.1f %.1f\n",
            (ui->frameTime - ui->launchTime) * 1e-6,
            ui->location[0], ui->location[1], ui->location[2],
            ui->eyeHeight,
            ui->facing, ui->tilt);
  }
  glm::vec3 eyes(0,0,ui->eyeHeight);

  double ha = DEG_TO_RAD(ui->facing);
  double va = DEG_TO_RAD(ui->tilt);
  glm::vec3 direction(cos(ha), sin(ha), 0);
  glm::vec3 right(direction[1], -direction[0], 0);
  glm::vec3 looking(cos(va) * cos(ha), cos(va) * sin(ha), sin(va));
  
  glm::vec3 up = glm::cross(right, looking);
  // eyes is the camera position in world coordinates
  eyes += ui->location;
  ui->current_viewpoint.eyes = eyes;
  ui->current_viewpoint.facing = ui->facing;
  ui->current_viewpoint.tilt = ui->tilt;
  /*printf("PWA eyes (%.3f %.3f %.3f) direction <%.3f %.3f %.3f>\n",
         eyes[0], eyes[1], eyes[2],
         looking[0], looking[1], looking[2]);*/
  ui->current_viewpoint.vp_matrix = glm::lookAt(eyes,
                                                eyes + looking,
                                                up);
}


void PlayerWalkingAnimus::complete()
{
  // flush the final parameter updates
  update(1.0);
  if (debug_animus) {
    fprintf(debug_animus, "%.6f complete_walking %u\n", 
            (pa_ui->frameTime - pa_ui->launchTime) * 1e-6,
            a_uid);
  }
  if (pa_ui->activePlayerAnimae & pa_keyFlags) {
    // hmm, they are still trying to walk;
    // create a new animus to replace this one, matching
    // in direction and speed
    PlayerWalkingAnimus *a = new PlayerWalkingAnimus();
    a->a_startTime = pa_ui->frameTime;
    a->a_endTime = pa_ui->frameTime + 1000000;
    a->a_subject = NULL;   // TODO, the player's entity
    a->a_uid = nextAnimusUid++;

    a->pa_ui = pa_ui;
    a->pa_keyFlags = pa_keyFlags;
    a->pwa_direction = pwa_direction;
    a->pwa_speed = pwa_speed;
    // since we ran the update(), we can just read the location
    // from the ui
    a->pwa_startloc = pa_ui->location;

    pa_ui->playerAnimae.push_back(a);
  }
}

void ui_flush_player_info(UserInterface *ui)
{
  wire::entity::EntityInfo ei;
  ei.set_uid(ui->playerEntity->id);
  ei.add_coords(ui->location[0]);
  ei.add_coords(ui->location[1]);
  ei.add_coords(ui->location[2]);
  ei.add_coords(ui->facing);
  ei.add_coords(ui->tilt);
  std::string buf;
  ei.SerializeToString(&buf);
  ui->cnx->send(wire::major::Major::ENTITY_ENTITY_INFO, buf);
}

void ui_cancel_animus(UserInterface *ui, Animus *a)
{
  //printf("cancel animus %p\n", a);
  long dt_i = ui->frameTime - a->a_startTime;
  double dt = dt_i / (double)(a->a_endTime - a->a_startTime);
  if (debug_animus) {
    fprintf(debug_animus, "%.6f cancel %u\n", 
            (ui->frameTime - ui->launchTime) * 1e-6, a->a_uid);
  }

  a->update(dt);
  ui_flush_player_info(ui);
  delete a;
}

/**
 *  This removes the given PlayerAnimus object with extreme
 *  prejudice; it does NOT delete it OR call its final update()
 *  method.  Should probably only be used from within an
 *  update handler to self-destruct, as in the case of canceling
 *  a walking animus when bumping into a wall.
 */

bool ui_remove_animus(UserInterface *ui, PlayerAnimus *a)
{
  std::vector<PlayerAnimus*> *pa = &ui->playerAnimae;

  unsigned j = 0;
  for (std::vector<PlayerAnimus*>::iterator i=pa->begin();
       i!=pa->end();
       ++j, ++i) {
    if (a == *i) {
      pa->erase(pa->begin() + j);
      return true;
    }
  }
  return false;
}

bool ui_cancel_animus(UserInterface *ui, unsigned keyFlagMask)
{
  std::vector<PlayerAnimus*> *pa = &ui->playerAnimae;

  unsigned j = 0;
  for (std::vector<PlayerAnimus*>::iterator i=pa->begin();
       i!=pa->end();
       ++j, ++i) {
    PlayerAnimus *a = *i;
    if (a->pa_keyFlags & keyFlagMask) {
      // found one; assume there is only one
      ui_cancel_animus(ui, a);
      pa->erase(pa->begin() + j);
      return true;
    }
  }
  // didn't find any
  /*
  printf("No keyFlags=%#x found: [", keyFlagMask);
  for (std::vector<PlayerAnimus*>::iterator i=pa->begin();
       i!=pa->end();
       ++j, ++i) {
    PlayerAnimus *a = *i;
    printf(" %p(%#x)", a, a->pa_keyFlags);
  }
  printf("]\n");
  */
  return false;
}

void ui_cancel_animae(UserInterface *ui, std::vector<PlayerAnimus*> *vec)
{
  for (std::vector<PlayerAnimus*>::iterator i=vec->begin();
       i!=vec->end();
       ++i) {
    ui_cancel_animus(ui, *i);
  }
  vec->clear();
}

void ui_hopup(UserInterface *ui, float dz, float dt)
{
  PlayerJumpAnimus *a = new PlayerJumpAnimus();
  long t = ui->frameTime;
  a->a_uid = nextAnimusUid++;
  a->a_startTime = t;
  a->a_endTime = t + dt*1e6;
  a->a_subject = NULL;   // TODO, the player's entity
  a->pa_ui = ui;
  a->pa_keyFlags = (1 << HOP);
  if (fabs(dz) <= 1.0*z_scale) {
    a->pja_jumpHeight = 0;      // linear slide
  } else if (fabs(dz) <= MAX_STEP_UP * z_scale / 2) {
    a->pja_jumpHeight = 0.5 * fabs(dz); // slight hop
  } else {
    a->pja_jumpHeight = 0.8 * fabs(dz); // hoppier hop
  }
  a->pja_initialAltitude = ui->location[2];
  a->pja_finalAltitude = ui->location[2] + dz;
  /*printf("hop %p by %.3f over %g (%ld)\n", 
         a, dz, dt, 
         a->a_endTime - a->a_startTime);*/
  if (debug_animus) {
    fprintf(debug_animus, "%.6f new_jump %u jumpHeight %.4f from %.4f to %.4f\n",
            (ui->frameTime - ui->launchTime) * 1e-6,
            a->a_uid,
            a->pja_jumpHeight,
            a->pja_initialAltitude,
            a->pja_finalAltitude);
  }
  ui->playerAnimae.push_back(a);
}

void ui_jump(UserInterface *ui)
{
  PlayerJumpAnimus *a = new PlayerJumpAnimus();
  long t = ui->frameTime;
  a->a_uid = nextAnimusUid++;
  a->a_startTime = t;
  a->a_endTime = t + 0.667*1000000;
  a->a_subject = NULL;   // TODO, the player's entity
  a->pa_ui = ui;
  a->pa_keyFlags = (1 << JUMP);
  a->pja_jumpHeight = 10 * z_scale;
  a->pja_initialAltitude = ui->location[2];
  a->pja_finalAltitude = ui->location[2];
  //printf("jump %p\n", a);
  ui->playerAnimae.push_back(a);
}


void ui_crouch(UserInterface *ui, bool crouchingDown)
{
  long t = ui->frameTime;
  // if there is a current one "in progress", clear it out
  // but flush its updates
  //ui_cancel_animae(ui, &ui->playerAnimae);

  PlayerCrouchAnimus *a = new PlayerCrouchAnimus();
  a->a_uid = nextAnimusUid++;
  a->a_startTime = t;
  a->a_endTime = t + 1000000/3;
  a->a_subject = NULL;   // TODO, the player's entity
  a->pa_ui = ui;
  a->pa_keyFlags = (1 << CROUCH);
  a->pca_crouchingDown = crouchingDown;
  a->pca_initialEyeHeight = ui->eyeHeight;
  //printf("crouch %p\n", a);
  ui->playerAnimae.push_back(a);
}

void ui_start_walking(UserInterface *ui, 
                      unsigned keyFlags,
                      float direction, 
                      float speed)
{
  long t = ui->frameTime;
  // if there is a current one "in progress", clear it out
  // but flush its updates
  //ui_cancel_animae(ui, &ui->playerAnimae);

  assert(!ui_cancel_animus(ui, keyFlags));

  PlayerWalkingAnimus *a = new PlayerWalkingAnimus();
  /*  printf("start walking %p (keyFlags %#x) direction %.3f speed %.2f\n", 
      a, keyFlags, direction, speed);*/

  a->a_uid = nextAnimusUid++;
  a->pa_ui = ui;
  a->pa_keyFlags = keyFlags;
  a->pwa_direction = direction;
  a->pwa_speed = speed;
  a->pwa_startloc = ui->location;
  a->a_startTime = t;
  a->a_endTime = t + 1000000;
  a->a_subject = NULL;   // TODO, the player's entity

  ui->playerAnimae.push_back(a);
}

void ui_process_user_commands(struct UserInterface *ui, float deltaTime)
{
  const static bool verbose = false;
  const char *move = NULL;

  bool changeOrientation = false;

  if (ui->focus) {
    if (ui->frame == 1) {
      SDL_WarpMouseInWindow(ui->window, 400, 300);
    } else {
      int xpos, ypos;
      SDL_GetMouseState(&xpos, &ypos);

      int dx = 400 - xpos;
      int dy = 300 - ypos;
      if (dx || dy) {
        // there is a change of orientation
        //ui_cancel_animae(ui, &ui->playerAnimae);

        /*printf( "%9lu ; mouse change %d, %d dt %g\n", 
          ui->frame, dx, dy, deltaTime);*/
        ui->facing = fmod(360 + (ui->facing + ui->mouseScale * deltaTime * dx),
                          360.0);
        ui->tilt += ui->mouseScale * deltaTime * dy;
        if (ui->tilt > 80) {
          ui->tilt = 80;
        } else if (ui->tilt < -80) {
          ui->tilt = -80;
        }
        /*printf("facing %.3f tilt %.3f\n", ui->facing, ui->tilt);*/
        move = "tilt";
        changeOrientation = true;
        SDL_WarpMouseInWindow(ui->window, 400, 300);
      }
    }
  }

  double va = ui->tilt * (PI / 180);
  double ha = ui->facing * (PI / 180);
  //  glm::vec3 direction(cos(va) * sin(ha), sin(va), cos(va) * cos(ha));
  glm::vec3 direction(cos(va) * cos(ha), cos(va) * sin(ha), sin(va));
  //glm::vec3 right = glm::vec3(sin(ha - PI/2), 0, cos(ha - PI/2));
  //glm::vec3 right = glm::vec3(sin(ha), -cos(ha), 0);
  //glm::vec3 up = glm::cross(right, direction);

  if (move) {
    if (verbose) {
      printf("direction %s <%.3f %.3f> = %.3f %.3f %.3f\n",
             move,
             ui->facing,
             ui->tilt,
             direction[0], direction[1], direction[2]);
    }
  }
  const unsigned char *keys = SDL_GetKeyboardState(NULL);

  unsigned newActivePlayerAnimae = 0;

  if (keys[keymap[WALK_FORWARD]]) {
    newActivePlayerAnimae |= (1 << WALK_FORWARD);
  } else if (keys[keymap[WALK_BACKWARD]]) {
    newActivePlayerAnimae |= (1 << WALK_BACKWARD);
  } else if (keys[keymap[SLIDE_LEFT]]) {
    newActivePlayerAnimae |= (1 << SLIDE_LEFT);
  } else if (keys[keymap[SLIDE_RIGHT]]) {
    newActivePlayerAnimae |= (1 << SLIDE_RIGHT);
  } else if (keys[keymap[RUN]]) {
    newActivePlayerAnimae |= (1 << RUN);
  }
  if (keys[keymap[CROUCH]]) {
    newActivePlayerAnimae |= (1 << CROUCH);
  }
  if (keys[keymap[JUMP]]) {
    newActivePlayerAnimae |= (1 << JUMP);
  }

  /*  if (changeOrientation) {
  // for now, cancel everything
  ui_cancel_animae(ui, &ui->playerAnimae);
  ui->activePlayerAnimae = 0;
  }
  */

  // there's no canceling a jump... so if a jump is in-progress, leave it
  if (ui->activePlayerAnimae & (1<<JUMP)) {
    newActivePlayerAnimae |= (1<<JUMP);
  }
  if (ui->activePlayerAnimae & (1<<HOP)) {
    newActivePlayerAnimae |= (1<<HOP);
  }

  unsigned changed = newActivePlayerAnimae ^ ui->activePlayerAnimae;

  if (changeOrientation && (newActivePlayerAnimae & ANY_WALKING_MASK)) {
    // orientation, and hence direction of motion, has changed;
    // make sure we't rebuild it as if there was a change in the
    // walking flag, even if there isn't one
    changed |= (newActivePlayerAnimae & ANY_WALKING_MASK);
  }

  if (changed) {
    //printf("Changed %#x (now %#x)\n", changed, newActivePlayerAnimae);
    // something has changed...
    // (although, see just above, it could be that we haven't
    // *actually* changed the walking status but only changed orientation)

    if (changed & ANY_WALKING_MASK) {
      ui_cancel_animus(ui, ANY_WALKING_MASK);

      if (newActivePlayerAnimae & ANY_WALKING_MASK) {
        float dir = 0;
        float speed = ui->walkSpeed;

        if (newActivePlayerAnimae & (1<<CROUCH)) {
          speed *= CROUCH_WALK_SPEED_FACTOR;
        } else if (newActivePlayerAnimae & (1<<RUN)) {
          speed *= RUN_SPEED_FACTOR;
        }

        switch (ui->spanOnTopOf.type) {
        case 5:
          speed *= RAILWAY_WALK_SPEED_FACTOR;
          break;
        }

        if (newActivePlayerAnimae & ((1<<WALK_FORWARD)|(1<<RUN))) {
          dir = ui->facing;
        } else if (newActivePlayerAnimae & (1<<WALK_BACKWARD)) {
          dir = fmod(ui->facing + 180, 360);
        } else if (newActivePlayerAnimae & (1<<SLIDE_RIGHT)) {
          dir = fmod(ui->facing - 90, 360);
        } else if (newActivePlayerAnimae & (1<<SLIDE_LEFT)) {
          dir = fmod(ui->facing + 90, 360);
        }
        ui_start_walking(ui, 
                         newActivePlayerAnimae & ANY_WALKING_MASK, 
                         dir, speed);
      }
    }
    if (changed & (1<<CROUCH)) {
      ui_cancel_animus(ui, (1<<CROUCH));
      ui_crouch(ui, (newActivePlayerAnimae & (1<<CROUCH)) ? true : false);
    }
    if (changed & (1<<JUMP)) {
      ui_jump(ui);
    }
    ui->activePlayerAnimae = newActivePlayerAnimae;
  }
}

void ui_update(struct UserInterface *ui, float deltaTime)
{
  glm::dvec3 loc(ui->location);
  double solar_time = (ui->frameTime - ui->solarTimeReference) * ui->solarTimeRate;
  ui->skyView.update(loc, solar_time);

  float timeofday = (sin(2*PI*ui->frameTime / 600e6) + 2)/3.0;
  ui->skyColor.r = 0x80/255.0 * timeofday;
  ui->skyColor.g = 0xd9/255.0 * timeofday;
  ui->skyColor.b = 1.0 * timeofday;

  if (ui->inputbox.popupTime == 0) {
    // updates from keyboard events
    ui_process_user_commands(ui, deltaTime);
  }

  // update entity locations and parameters based on animations

  // close out completed animae
  // (note that we capture numAnimae before looping through,
  // so that any new animae created during complete() invocations
  // will not be processed this time)
  size_t numAnimae = ui->playerAnimae.size();
  long t = ui->frameTime;

  for (unsigned i=0; i<numAnimae; i++) {
    PlayerAnimus *a = ui->playerAnimae[i];
    if (t >= a->a_endTime) {
      a->complete();
      delete a;
      ui->playerAnimae.erase(ui->playerAnimae.begin() + i);
      i--;
      numAnimae--;
    }
  }

  // update inprogress animae

  // make a copy so new stuff can be added (e.g., a hop-up animus
  // added while walking) or removed (e.g., crashing into a wall
  // while walking) on the fly
  std::vector<PlayerAnimus*> tmp = ui->playerAnimae;

  for (std::vector<PlayerAnimus*>::iterator i = tmp.begin();
       i != tmp.end();
       ++i) {
    PlayerAnimus *a = *i;
    assert(t < a->a_endTime);   // the other case was handled already
    double dt = (t-a->a_startTime) / (double)(a->a_endTime - a->a_startTime);
    if (!a->update(dt)) {
      ui_remove_animus(ui, a);
      delete a;
    }
  }
  ui_update_viewpoint(ui);
  ui_popup_update(ui);
}

/* max distance of any point within a hex from the center of the hex */

static const double maxHexDistance = 2 * a;     // same as edge; look at the triangle decomposition

static const double PROXIMITY_RADIUS = (0.5+0.577)/2;

double intersection_parameter(glm::vec2 const& origin,
                              glm::vec2 const& direction,
                              glm::vec2 const& point)
{
  double d_x = direction[0];
  double d_y = direction[1];
  double p_ax = point[0] - origin[0];
  double p_ay = point[1] - origin[1];
  return ((p_ax * d_x) + (p_ay * d_y))
    / ((d_x * d_x) + (d_y * d_y));
}

struct RegionCandidateHit {
  ClientRegion *rgn;
  double t0, t1;
  PickerPtr p;
  static int cmp(const void *a, const void *b);
};

int RegionCandidateHit::cmp(const void *a, const void *b)
{
  const RegionCandidateHit *ap = (RegionCandidateHit *)a;
  const RegionCandidateHit *bp = (RegionCandidateHit *)b;
  if (ap->t1 < bp->t1) {
    return -1;
  } else {
    return 1;
  }
}

void ui_outline_hit(struct UserInterface *ui)
{
  /* figure out what is in front of the cursor */

  double va = ui->tilt * (PI / 180);
  double ha = ui->facing * (PI / 180);
  glm::vec3 direction(cos(va) * cos(ha), cos(va) * sin(ha), sin(va));
  glm::vec3 eyes = ui->location;
  eyes += glm::vec3(0,0,ui->eyeHeight);

  ui->pick.enable = false;
  ui->pick.entity = ~0;

  // check the entities
  for (EntityMap::iterator i=ui->entities.begin(); i!=ui->entities.end(); ++i) {
    if (i->second->handler) {
      PickPoint ep;
      // translate our eyes into the entity's coordinate system
      Entity *e = i->second;
      glm::vec3 home_eyes = eyes - e->location;
      int rc = e->pick(home_eyes, direction, &ep);
      if (rc == 0) {
        //printf("**************** picking entity %p\n", i->second);
        ui->pick.enable = true;
        ui->pick.entity = e->id;
        convert_xyz_to_hex(e->location,
                           &ui->pick.x,
                           &ui->pick.y,
                           &ui->pick.z);
        show_box(ui, e->bbox());
        return;
      }
    }
  }

  // check the terrain
  //  sort the regions by closest-hit distance
    
  RegionCandidateHit rch[20];
  unsigned rch_count = 0;

  for (regionCacheType::iterator j = ui->world->state.regionCache.begin();
       (rch_count < 20) && (j != ui->world->state.regionCache.end());
       ++j) {
    ClientRegion *rgn = j->second;
    PickerPtr p(rgn->picker);
    double t0, t1;

    int rc = p->closest(eyes, direction, &t0, &t1);
    if (rc >= 0) {
      rch[rch_count].rgn = rgn;
      rch[rch_count].t0 = t0;
      rch[rch_count].t1 = t1;
      rch[rch_count].p = p;
      rch_count++;
    }
  }

  qsort(&rch[0], rch_count, sizeof(rch[0]), RegionCandidateHit::cmp);

  /*if (rch_count > 0) {
    printf("\n");
    for (unsigned i=0; i<rch_count; i++) {
      printf("  RCH[%u] (%d,%d) %.4f %.4f\n",
             i,
             rch[i].rgn->origin.x, rch[i].rgn->origin.y,
             rch[i].t0, rch[i].t1);
    }
    }*/
  
  for (unsigned i=0; i<rch_count; i++) {
    ClientRegion *rgn = rch[i].rgn;
    PickerPtr p(rch[i].p);
    int rc;
    PickPoint pp;

    rc = p->pick(eyes, direction, rch[i].t0, rch[i].t1, &pp);
    if ((rc < 0) || (pp.range > SELECTION_RANGE)) {
      //printf("  no hit after all\n");
    } else {
      RegionPickIndex px(pp.index);
      /*printf("  hit at %#lx %ld range %.3f:  x=%2d y=%d face=%d z0=%4d si=%4d hitz=%4d\n", 
             pp.index, pp.index, pp.range,
             px.x(), px.y(), px.face(), px.z0(), px.span(), px.hitz());*/
      /*if (px.face() == PICK_INDEX_FACE_TOP) {
        } else if (px.face() == PICK_INDEX_FACE_BOTTOM) {
        } else {*/ 
      int z0 = rgn->basement + px.z0();
      int ze = rgn->basement + px.hitz();
      SpanVector const& span(rgn->columns[px.y()][px.x()]);
      outline_hex_prism(ui, 
                        rgn->origin.x + px.x(), 
                        rgn->origin.y + px.y(), 
                        z0,
                        z0 + span[px.span()].height);
      int face_z0, face_z1;
      switch (px.face()) {
      case PICK_INDEX_FACE_TOP:
      case PICK_INDEX_FACE_BOTTOM:
        face_z0 = z0;
        face_z1 = z0 + span[px.span()].height;
        break;
      default:
        // pick out a single slab at Z_entry
        face_z0 = ze;
        face_z1 = ze+1;
        break;
      }
      outline_hex_face(ui, 
                       rgn->origin.x + px.x(), 
                       rgn->origin.y + px.y(), 
                       face_z0,
                       face_z1,
                       px.face());
      ui->pick.x = rgn->origin.x + px.x();
      ui->pick.y = rgn->origin.y + px.y();
      ui->pick.z = z0 + span[px.span()].height;
      ui->pick.p_rpi = pp.index;
      ui->pick.p_rgn = rgn;
      ui->pick.enable = true;
      return;
    }
  }
}

static const float status_window_scale = 72.0;
static const float status_window_x_origin = 320;
static const float status_window_y_origin = 320;

static inline SDL_Point status_window_xform(dvec2 const& p)
{
  SDL_Point i;

  i.x = p.x * status_window_scale + status_window_x_origin;
  i.y = p.y * -status_window_scale + status_window_y_origin;
  return i;
}

void ui_render_status(struct UserInterface *ui)
{
  SDL_Renderer *rend = ui->status_renderer;
  if (!rend) {
    return;
  }

  SDL_SetRenderDrawColor(rend, 230, 230, 230, 255);
  SDL_RenderClear(rend);

  {
    // draw the axes
    SDL_SetRenderDrawColor(rend, 250, 100, 100, 255);
    SDL_Point pts[2];
    pts[0] = status_window_xform(dvec2(-3 * x_stride,0));
    pts[1] = status_window_xform(dvec2(3 * x_stride,0));
    SDL_RenderDrawLines(rend, &pts[0], 2);
    pts[0] = status_window_xform(dvec2(0, -3 * y_stride));
    pts[1] = status_window_xform(dvec2(0, 3 * y_stride));
    SDL_RenderDrawLines(rend, &pts[0], 2);
  }

  SDL_SetRenderDrawColor(rend, 100, 100, 250, 255);
  for (int dx=-3; dx<=3; dx++) {
    for (int dy=-3; dy<=3; dy++) {
      SDL_Point pts[7];
      dvec2 hex_origin = dvec2(hex_x(dx, dy),hex_y(dx, dy));
      for (int i=0; i<6; i++) {
        pts[i] = status_window_xform(hex_origin + hex_edges[i].online);
      }
      pts[6] = pts[0];
      SDL_RenderDrawLines(rend, &pts[0], 7);
    }
  }

  if (status.show_adj) {
    SDL_Point pts[2];
    dvec2 hex_origin = dvec2(hex_x(status.adj.x, status.adj.y),
                             hex_y(status.adj.x, status.adj.y));
    SDL_SetRenderDrawColor(rend, 255, 0, 0, 255);
    pts[0] = status_window_xform(dvec2(hex_center_x(status.adj.x, status.adj.y),
                                       hex_center_y(status.adj.x, status.adj.y)));
    for (int i=0; i<6; i++) {
      pts[1] = status_window_xform(hex_origin + hex_edges[i].online);
      SDL_RenderDrawLines(rend, &pts[0], 2);
    }
  }


  // draw where the player is
  {
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
    dvec2 fwd(cos(DEG_TO_RAD(ui->facing)),
              sin(DEG_TO_RAD(ui->facing)));
    dvec2 origin(ui->location[0],ui->location[1]);
    dvec2 left(-fwd.y, fwd.x);

    SDL_Point pts[4];
    pts[0] = status_window_xform(origin);
    pts[1] = status_window_xform(origin + fwd);
    SDL_RenderDrawLines(rend, &pts[0], 2);

    pts[0] = status_window_xform(origin + fwd);
    pts[1] = status_window_xform(origin + fwd * -0.333 + left * 0.2);
    pts[2] = status_window_xform(origin + fwd * -0.333 - left * 0.2);
    pts[3] = pts[0];
    SDL_RenderDrawLines(rend, &pts[0], 4);
  }


#if HAVE_SDL2_TTF
  if (status.msg1[0] != '\0') {
    SDL_Color fg = {0,0,0};
    SDL_Surface *x = TTF_RenderText_Solid(ui->popupFont, status.msg1, fg);
    assert(x);
    if (x) {
      SDL_Texture *tx = SDL_CreateTextureFromSurface(rend, x);
      assert(tx);
      if (tx) {
        SDL_Rect dstr;
        dstr.x = 20;
        dstr.y = 20;
        dstr.w = x->w;
        dstr.h = x->h;
        SDL_RenderCopy(rend, tx, NULL, &dstr);
        SDL_DestroyTexture(tx);
      }
      SDL_FreeSurface(x);
    }
  }
#endif /* HAVE_SDL2_TTF */

  SDL_RenderPresent(rend);
}

void ui_render(struct UserInterface *ui)
{
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glClearColor(0.5/*ui->skyColor.r*/, 
               0.5/*ui->skyColor.g*/, 
               0.5/*ui->skyColor.b*/, 
               1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // configure texture unit 2 for the skybox texture
  glUseProgram(ui->skyboxShader.shaderId);
  glActiveTexture(GL_TEXTURE2);
  glUniform1i(ui->skyboxShader.shaderPgmTextureIndex, 2);

  glBindTexture(GL_TEXTURE_CUBE_MAP, cube_map_texture_id);

  //glBindTexture(GL_TEXTURE_2D, cube_map_texture_id);

  glm::mat4 MV = ui->current_viewpoint.vp_matrix;
  // remove the translation because we want the skybox to follow us
  MV[3][0] = 0;
  MV[3][1] = 0;
  MV[3][2] = 0;

  // inverse transformation so we can calculate world orientation from
  // the "device" coordinates which is where we render the quad
  glm::mat4 MVP        =  glm::inverse(MV) * glm::inverse(ui->projectionMatrix);

  // Send our transformation to the currently bound shader, in the
  // "MVP" uniform For each model you render, since the MVP will be
  // different (at least the M part)
  glUniformMatrix4fv(ui->skyboxShader.shaderPgmMVPMatrixIndex, 1, GL_FALSE, &MVP[0][0]);

  glUniform3f(ui->skyboxShader.sunPositionIndex,
              ui->skyView.sun.x,
              ui->skyView.sun.y,
              ui->skyView.sun.z);
  glUniformMatrix4fv(ui->skyboxShader.starMatrixIndex,
                     1, GL_FALSE, &ui->skyView.starMatrix[0][0]);

  glBegin(GL_QUADS);
  glVertex3f(-1.0, -1.0, 0.0);
  glVertex3f( 1.0, -1.0, 0.0);
  glVertex3f( 1.0,  1.0, 0.0);
  glVertex3f(-1.0,  1.0, 0.0);
  glEnd();

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_CULL_FACE);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ui->textureId);
  //printf("rendering terrain:");
  std::vector<Mesh*> water;

  for (std::vector<TerrainSection>::iterator m=ui->terrain.begin(); m!=ui->terrain.end(); ++m) {
    Posn p(m->ts_posn);
    glm::mat4 identity(1);
    glm::vec2 terrain_center(hex_center_x(p.x + REGION_SIZE/2,
                                          p.y + REGION_SIZE/2),
                             hex_center_y(p.x + REGION_SIZE/2,
                                          p.y + REGION_SIZE/2));
    glm::vec2 camera(ui->location.x, ui->location.y);
    if (glm::distance(terrain_center, camera) < 5*REGION_SIZE) {
      Mesh *mesh = m->ts_ground;
      mesh->render(ui, identity);
      if (m->ts_water) {
        water.push_back(m->ts_water);
      }
    }
  }
  if (!water.empty()) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glm::mat4 identity(1);
    for (std::vector<Mesh*>::iterator m=water.begin(); m!=water.end(); ++m) {
      (*m)->render(ui, identity);
    }
    glDisable(GL_BLEND);
  }

  //printf("\n");

  glActiveTexture(GL_TEXTURE0);
  for (EntityMap::iterator i=ui->entities.begin(); i!=ui->entities.end(); ++i) {
    if (i->second->handler) {
      i->second->handler->draw(ui, i->second);
    }
  }

  ui_outline_hit(ui);

  // draw the text popup
  ui_popup_draw(ui);
  // draw the tool popup
  if (ui_draw_overlay(ui, ui->toolPopup, 0, 0)) {
    float dy = (ui->toolPopup->ow_y1 - ui->toolPopup->ow_y0)/8;
    ui_draw_overlay(ui, ui->toolPopupSel, 
                    0, 
                    (ui->toolSlot-7) * dy);
  }

  glDisable(GL_BLEND);
}

const char *load_file(const char *path, size_t *len)
{
  FILE *f = fopen(path, "r");
  fseek(f, 0, SEEK_END);
  size_t n = ftell(f);
  char *buf = (char*)malloc(n+1);
  fseek(f, 0, SEEK_SET);
  fread(buf, 1, n, f);
  fclose(f);
  buf[n] = '\0';
  if (len) {
    *len = n;
  }
  return buf;
}

GLuint ui_load_texture(const wire::model::Image *img)
{
  unsigned width = img->width();
  unsigned height = img->height();
  unsigned num_bytes = width * height * sizeof(uint16_t) * 4;
  uint8_t *rgba_buf = (uint8_t *)malloc(num_bytes);
  uLongf rgba_buf_len = num_bytes;
  std::string const& payload(img->data());
  int rc = uncompress(rgba_buf, &rgba_buf_len, 
                      (uint8_t*)payload.data(), payload.size());
  assert(rc == 0);
  assert(rgba_buf_len == num_bytes);

  GLuint id;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);

  // rearrange the data
  unsigned char *texdata = (unsigned char *)malloc(width*height*4);
  uint16_t *red_ptr = (uint16_t*)&rgba_buf[0];
  uint16_t *green_ptr = (uint16_t*)&rgba_buf[width*height*sizeof(uint16_t)];
  uint16_t *blue_ptr = (uint16_t*)&rgba_buf[2*width*height*sizeof(uint16_t)];
  uint16_t *alpha_ptr = (uint16_t*)&rgba_buf[3*width*height*sizeof(uint16_t)];

  for (unsigned y=0; y<height; y++) {
    unsigned src_i = y * width;
    unsigned dst_i = ((height-1) - y) * width;
    for (unsigned x=0; x<width; x++) {
      texdata[4*dst_i+0] = red_ptr[src_i];
      texdata[4*dst_i+1] = green_ptr[src_i];
      texdata[4*dst_i+2] = blue_ptr[src_i];
      texdata[4*dst_i+3] = alpha_ptr[src_i];
      dst_i++;
      src_i++;
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
               width,
               height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               texdata);
  free(texdata);
  free(rgba_buf);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);

  return id;
}

GLuint ui_load_textures(const char *png_path)
{
  Picture *pict = Picture::load_png(png_path);
  if (!pict) {
    fprintf(stderr, "%s: Could not load texture\n", png_path);
    abort();
  }

  GLuint id;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);

  // rearrange the data
  unsigned w = pict->width;
  unsigned h = pict->height;
  unsigned char *texdata = (unsigned char *)malloc(w*h*4);
  memset(texdata, 0, w*h*4);
  unsigned char *texdata_ptr = texdata;
  for (unsigned y=0; y<h; y++) {
    for (unsigned x=0; x<w; x++) {
      Pixel p = pict->get_pixel(x, y);
      texdata_ptr[0] = p.r >> 8;
      texdata_ptr[1] = p.g >> 8;
      texdata_ptr[2] = p.b >> 8;
      texdata_ptr[3] = p.a >> 8;
      texdata_ptr += 4;
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
               w,
               h,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               texdata);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);
  free(texdata);

  return id;
}


GLuint load_shader(const char *path, int type)
{
  GLuint id = glCreateShader(type);
  const char *src = load_file(path, NULL);
  GLint rc = GL_FALSE;

  glShaderSource(id, 1, &src , NULL);
  glCompileShader(id);

  // Check compilation results
  glGetShaderiv(id, GL_COMPILE_STATUS, &rc);
  if (!rc) {
    int msglen;
    glGetShaderiv(id, GL_INFO_LOG_LENGTH, &msglen);
    char *msg = (char*)malloc(msglen+1);
    msg[msglen] = '\0';
    glGetShaderInfoLog(id, msglen, NULL, msg);
    fatal("glCompilerShader", msg);
  }
  return id;
}

GLuint load_shader_program(const char *vertex, const char *frag)
{
  GLuint vertex_shader = load_shader(vertex, GL_VERTEX_SHADER);
  GLuint frag_shader = load_shader(frag, GL_FRAGMENT_SHADER);

  GLuint id = glCreateProgram();
  glAttachShader(id, vertex_shader);
  glAttachShader(id, frag_shader);
  glLinkProgram(id);

  // Check the program
  GLint rc = GL_FALSE;
  glGetProgramiv(id, GL_LINK_STATUS, &rc);
  if (!rc) {

    int msglen;
    glGetProgramiv(id, GL_INFO_LOG_LENGTH, &msglen);
    char *msg = (char *)malloc(msglen+1);
    msg[msglen] = '\0';
    glGetProgramInfoLog(id, msglen, NULL, msg);
    fatal("glLinkProgram", msg);
  }
  glDeleteShader(vertex_shader);
  glDeleteShader(frag_shader);
  return id;
}


long real_time(void)    // real time in microseconds
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

void remesh_region(struct UserInterface *ui, ClientRegion *rgn)
{
  TerrainSection s = build_section_from_region(ui, rgn);

  for (std::vector<TerrainSection>::iterator i=ui->terrain.begin(); i!=ui->terrain.end(); ++i) {
    if ((i->ts_posn.x == rgn->origin.x)
        && (i->ts_posn.y == rgn->origin.y)) {
      i->releaseContents();
      ui->terrain.erase(i);
      break;
    }
  }
  ui->terrain.push_back(s);
}

void place_block(struct UserInterface *ui)
{
  ClientRegion *rgn;
  SpanVector *vec = ui->world->getColumn(ui->pick.x, 
                                         ui->pick.y, 
                                         &rgn);
  vec->back().height += 1;
  //ui->mainmesh = build_mesh_from_region(ui, rgn);
  printf("place_block() h=%d\n", vec->back().height);
  remesh_region(ui, rgn);
}

void destroy_block(struct UserInterface *ui)
{
  ClientRegion *rgn;
  SpanVector *vec = ui->world->getColumn(ui->pick.x, 
                                         ui->pick.y, 
                                         &rgn);
  if (vec->back().height == 1) {
    vec->pop_back();
  } else {
    vec->back().height -= 1;
  }
  //ui->mainmesh = build_mesh_from_region(ui, rgn);
  remesh_region(ui, rgn);
}

static const char shifted[] = "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ1!2@3#4$5%6^7&8*9(0)-_=+[{]}\\|;:'\",<.>/?`~";

int my_toascii(SDL_Event *event)
{
  char code = event->key.keysym.sym;

  // EVIL!  Why am I not getting TEXTINPUT events?
  if (event->key.keysym.mod & (KMOD_LSHIFT|KMOD_RSHIFT)) {
    for (int i=0; shifted[i*2]; i++) {
      if (shifted[i*2] == code) {
        return shifted[i*2+1];
      }
    }
  }
  return code;
}

void ui_process_command(UserInterface *ui, std::string const& text)
{
  if (text[0] == '/') {
    tell_entity(ui, text.substr(1));
    return;
  }
  if (text.substr(0,1) == "h") {
    int v = std::stoi(text.substr(1));
    if (v > 0) {
      ui->toolHeight = v;
    }
  }
  if (text.substr(0,1) == "t") {
    int v = std::stoi(text.substr(1));
    if (v > 0) {
      ui->placeToolType = v;
    }
  }
  
}

void ui_process_key_inputbox(struct UserInterface *ui, SDL_Event *event)
{
  InputBox *b = &ui->inputbox;
  b->popupTime = ui->frameTime;
  b->dirty = true;

  if (event->key.type == SDL_KEYDOWN) {
    switch (event->key.keysym.scancode) {
    case SDL_SCANCODE_RIGHT:
      if (b->cursor < b->text.size()) {
        b->cursor++;
      }
      return;
    case SDL_SCANCODE_LEFT:
      if (b->cursor > 0) {
        b->cursor--;
      }
      return;
      
    case SDL_SCANCODE_HOME:
      b->cursor = 0;
      return;
    case SDL_SCANCODE_END:
      b->cursor = b->text.size();
      return;
    case SDL_SCANCODE_DELETE:
      if (b->cursor < b->text.size()) {
        b->text.erase(b->cursor, 1);
      }
      return;
    default:
      // fall through
      ;
    }

    switch (event->key.keysym.sym) {
    case SDLK_RETURN:
      b->popupTime = 0;
      SDL_StopTextInput();
      if (b->text.size() > 0) {
        ui_process_command(ui, b->text);
      }
      b->text.clear();
      break;

    case SDLK_BACKSPACE:
      if (b->cursor > 0) {
        b->text.erase(b->cursor-1, 1);
        b->cursor--;
      }
      break;

    default:
      if ((event->key.keysym.sym >= ' ') && (event->key.keysym.sym < 0x7F)) {
        char ch = my_toascii(event);
        printf("keydown... '%c'\n", ch);
        b->text.insert(b->cursor, 1, ch);
        b->cursor++;
      } else {
        printf("keydown... %#x ignored\n", event->key.keysym.sym);
      }
    }
    return;
  }
  printf("input event \"%s\"\n", event->text.text);
  b->text.insert(b->cursor, event->text.text);
  b->cursor++;
}

void ui_process_key(struct UserInterface *ui, SDL_Event *event)
{
  switch (event->key.keysym.sym) {
  case SDLK_TAB:
    if (event->key.type == SDL_KEYDOWN) {
      printf("switch focus\n");
      ui->focus = !ui->focus;
      if (ui->focus) {
        // if switching back to focus mode, warp the mouse
        SDL_WarpMouseInWindow(ui->window, 400, 300);
      }
    }
    return;
  case SDLK_ESCAPE:
    if (event->key.type == SDL_KEYDOWN) {
      if (ui->inputbox.popupTime) {
        ui->inputbox.popupTime = 0; // turn off inputbox, if present
        SDL_StopTextInput();
      } else {
        // otherwise, exit the app
        ui->done_flag = SDL_TRUE;
      }
    }
    return;

  case SDLK_F1:
    if (!ui->status_window) {
      ui->status_window = SDL_CreateWindow("Status",
                                           910,
                                           100,
                                           STATUS_WINDOW_WIDTH, STATUS_WINDOW_HEIGHT,
                                           0 /* no flags */);
      if (!ui->status_window) {
        fatal("SDL_CreateWindow", SDL_GetError());
      }
      ui->status_renderer = SDL_CreateRenderer(ui->status_window, -1, SDL_RENDERER_SOFTWARE);
      if (!ui->status_renderer) {
        fatal("SDL_CreateRenderer", SDL_GetError());
      }
      SDL_SetRenderDrawColor(ui->status_renderer, 255, 255, 255, 255);
      SDL_RenderClear(ui->status_renderer);
      SDL_RenderPresent(ui->status_renderer);
    }
    return;
  }

  // everything else gets handled differently if we are in inputbox mode
  if (ui->inputbox.popupTime) {
    if (event->key.type == SDL_KEYDOWN) {
      ui_process_key_inputbox(ui, event);
    } else if (event->key.type == SDL_TEXTINPUT) {
      ui_process_key_inputbox(ui, event);
    }
  } else {
    if (event->key.type == SDL_KEYDOWN) {
      switch (event->key.keysym.sym) {
      case SDLK_SLASH:
        ui_inputbox_start_typing(ui);
        SDL_StartTextInput();
        break;
      case SDLK_x:
        /* destroy block(s) */
        if (ui->pick.enable) {
          destroy_block(ui);
        }
        break;
      case SDLK_p:
        /* place block(s) */
        if (ui->pick.enable) {
          place_block(ui);
        }
        break;
      }
    }
  }
}

struct GUIWireHandler : WireHandler {
  virtual void dispatch(wire::terrain::Terrain *);
  virtual void dispatch(wire::hello::ServerGreeting *);
  virtual void dispatch(wire::entity::PlayerStatus *);
  virtual void dispatch(wire::entity::EntityInfo *);
  virtual void dispatch(wire::entity::EntityType *);
  virtual void dispatch(wire::entity::Tell *);
  UserInterface *ui;
};

void GUIWireHandler::dispatch(wire::entity::Tell *msg)
{
  printf("Got told (#%u) \"%s\"\n", msg->target(), msg->message().c_str());
}

void GUIWireHandler::dispatch(wire::terrain::Terrain *msg)
{
  printf("Got terrain data\n");
  wire::terrain::Rect const& area = msg->area();
  printf("   (%d,%d) size (%d,%d)\n", area.x(), area.y(), area.w(), area.h());
  bool verbose = false;
  /*if ((area.x() == 0) && (area.y() == 0)) {
    verbose = true;
    }*/

  World *w = &ui->world->state;

  ClientRegion *rgn = new ClientRegion();
  rgn->origin.x = area.x();
  rgn->origin.y = area.y();
  rgn->basement = msg->basement();

  // Decode the span array
  unsigned char *p0 = (unsigned char *)&msg->spanarray()[0];
  unsigned char *p = p0;

  for (int y=0; y<REGION_SIZE; y++) {
    for (int x=0; x<REGION_SIZE; x++) {
      bool cverbose = (verbose && (x == 0) && (y == 2));
      std::vector<Span>& col = rgn->columns[y][x];
      while (*p) {
        if (cverbose) {
          printf(": %02x %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4], p[5]);
        }
        unsigned h = *p++;
        if (h == 0x7F) {
          h = ((unsigned)p[0] << 8) + p[1];
          p += 2;
        }
        Span s;
        s.height = h;
        s.type = p[0];
        s.flags = p[1];
        p += 2;
        if (cverbose) {
          printf("decoded (%d,%d) - h=%d t=%d f=%d\n", x, y, s.height, s.type, s.flags);
        }
        col.push_back(s);
      }
      if (cverbose) {
        printf("  col has %ld spans\n", col.size());
      }
      p++;
    }
  }

  regionCacheType::iterator j = w->regionCache.find(rgn->origin);
  if (j != w->regionCache.end()) {
    w->regionCache.erase(j);
  }
  w->regionCache.insert(regionCacheType::value_type(rgn->origin, rgn));

  remesh_region(ui, rgn);
}

void GUIWireHandler::dispatch(wire::entity::EntityType *etype)
{
  std::string n(etype->type());

  printf("Got entity type \"%s\" parent=\"%s\"\n", 
         etype->type().c_str(),
         etype->has_parent() ? etype->parent().c_str() : "--");
  ui->etypes.insert(EntityTypeMap::value_type(n,etype));
}

void GUIWireHandler::dispatch(wire::hello::ServerGreeting *msg)
{
  printf("Got server greeting\n");
  printf("  Server name \"%s\"\n", msg->servername().c_str());
  printf("  Users %d/%d\n", msg->current_users(), msg->max_users());
  if (msg->has_resultcode()) {
    printf("  Result Code %d\n", msg->resultcode());
    if (msg->has_message()) {
      printf("  Message: %s\n", msg->message().c_str());
    }
    exit(1);
  }
  if (msg->has_solartime()) {
    printf("  Solar time: %.3f\n", msg->solartime());
    ui->solarTimeBase = msg->solartime();
    ui->solarTimeReference = ui->frameTime;
  }
  if (msg->has_solardayreal()) {
    printf("  Solar day unit: %.3f sec\n", msg->solardayreal());
    // solar time rate is solar day-units per realtime unit (microsecond)
    // so we need to multiply by 10^-6 to get seconds
    // and then divide by the length of the solar day in wall clock seconds
    ui->solarTimeRate = 1.0e-6 / msg->solardayreal();
  }
  wire::entity::BecomePlayer bp;
  bp.set_playername("Alice");
  std::string buf;
  bp.SerializeToString(&buf);
  ui->cnx->send(wire::major::Major::ENTITY_BECOME_PLAYER, buf);
}

Entity *newEntityFromWire(UserInterface *ui, wire::entity::EntityInfo const& info)
{
  Entity *e = new Entity();
  e->id = info.uid();
  e->location = glm::vec3(0,0,0);
  e->facing = 0;
  e->tilt = 0;
  e->handler = NULL;

  if (info.has_type()) {
    std::string type(info.type());
    std::string sub;
    if (info.has_subtype()) {
      sub = info.subtype();
    }
    e->handler = EntityHandler::get(ui, type, sub);
  }
  unsigned l = info.coords_size();

  if (l >= 3) {
    e->location = glm::vec3(info.coords(0),
                            info.coords(1),
                            info.coords(2));
  }
  if (l >= 4) {
    e->facing = info.coords(3);
  }
  if (l >= 5) {
    e->tilt = info.coords(4);
  }

  return e;
}

void GUIWireHandler::dispatch(wire::entity::PlayerStatus *msg)
{
  printf("Got player status for \"%s\" (%d entities)\n", 
         msg->playername().c_str(),
         msg->entities_size());
  for (int i=0; i<msg->entities_size(); i++) {
    wire::entity::EntityInfo const& ei = msg->entities(i);
    printf("   [%u] entity #%u <%s>\n", i, ei.uid(), ei.type().c_str());
    if (ei.name() == msg->playername()) {
      printf("   *loaded player data*\n");
      ui->location[0] = ei.coords(0);
      ui->location[1] = ei.coords(1);
      ui->location[2] = ei.coords(2);
      ui->facing = ei.coords(3);
      ui->tilt = ei.coords(4);
      ui->playerEntity = newEntityFromWire(ui, ei);
    }
  }
  // request view data
  int hex_x, hex_y;
  convert_xy_to_hex(ui->location[0], 
                    ui->location[1],
                    &hex_x, &hex_y);
  hex_x &= ~(REGION_SIZE-1);
  hex_y &= ~(REGION_SIZE-1);
  for (int drx=-1; drx<=1; drx++) {
    for (int dry=-1; dry<=1; dry++) {
      ui->cnx->request_view(hex_x + REGION_SIZE*drx, hex_y + REGION_SIZE*dry);
    }
  }
}

void GUIWireHandler::dispatch(wire::entity::EntityInfo *msg)
{
  ui->update_entity(*msg);
}

struct SimpleEntityHandler : EntityHandler {
  SimpleEntityHandler(UserInterface *ui, wire::entity::EntityType *et);
  SuperMesh *mesh;
  virtual void draw(UserInterface *ui, Entity *ent);
  virtual int pick(glm::vec3 const& origin,
                   glm::vec3 const& direction,
                   PickPoint *pickat);

  GLuint textureId;
};

const wire::model::Image *find_image(UserInterface *ui, 
                                     wire::entity::EntityType const& et,
                                     std::string const& name)
{
  printf("checking <%s> for \"%s\"\n", et.type().c_str(), name.c_str());
  unsigned n = et.attr_size();
  for (unsigned i=0; i<n; i++) {
    wire::entity::EntityTypeAttr const& ea = et.attr(i);
    printf("   checking \"%s\"\n", ea.key().c_str());
    if (ea.key() == name) {
      if (ea.has_image_value()) {
        return &ea.image_value();
      }
      printf("key match but no value\n");
      return NULL;
    }
  }
  return NULL;
}

const GLuint load_texture(UserInterface *ui, 
                          wire::entity::EntityType const& et,
                          std::string const& name)
{
  static std::unordered_map<std::string,GLuint> loaded_textures;

  std::string key = et.type();
  key.push_back('#');
  key.append(name);

  std::unordered_map<std::string,GLuint>::iterator i;
  i = loaded_textures.find(key);
  if (i != loaded_textures.end()) {
    return i->second;
  }
  const wire::model::Image *img = find_image(ui, et, name);
  if (!img) {
    printf("No image \"%s\" found on entity type \"%s\"\n", 
           name.c_str(),
           et.type().c_str());
    // fallback
    return ui->cursorTexture;
  }
  GLuint v = ui_load_texture(img);

  printf("Constructed new texture map for \"%s\": %u\n", key.c_str(), v);
  loaded_textures.insert(std::unordered_map<std::string,GLuint>::value_type(key,v));
  return v;
}

const wire::model::Mesh *find_mesh(UserInterface *ui, 
                                   wire::entity::EntityType const& et,
                                   std::string const& name)
{
  printf("checking <%s> for \"%s\"\n", et.type().c_str(), name.c_str());
  unsigned n = et.attr_size();
  for (unsigned i=0; i<n; i++) {
    wire::entity::EntityTypeAttr const& ea = et.attr(i);
    printf("   checking \"%s\"\n", ea.key().c_str());
    if (ea.key() == name) {
      if (ea.has_mesh_value()) {
        return &ea.mesh_value();
      }
      printf("key match but no value\n");
      return NULL;
    }
  }
  return NULL;
}

SimpleEntityHandler::SimpleEntityHandler(UserInterface *ui, 
                                         wire::entity::EntityType *et)
{
  const wire::model::Mesh *m;
  
  textureId = load_texture(ui, *et, "texture");

  m = find_mesh(ui, *et, "mesh");
  if (!m) {
    printf("!! not found\n");
    mesh = NULL;
  } else {
    printf("FOUND...\n");
    mesh = internalize_model_mesh(*m, &ui->robotShader, &bbox, glm::mat4(1));
    mesh->picker = make_simple_picker(bbox);
  }
}

int SimpleEntityHandler::pick(glm::vec3 const& origin,
                              glm::vec3 const& direction,
                              PickPoint *pickat)
{
  if (mesh->picker) {
    PickerPtr p( mesh->picker );
    double t0, t1;
    int rc = p->closest(origin, direction, &t0, &t1);
    if (rc >= 0) {
      /*printf("  entity intersection is possible, between %.3f and %.3f\n", 
        t0, t1);*/
      return p->pick(origin, direction, t0, t1, pickat);
    }
  }
  return -1;
}


void show_box(UserInterface *ui, frect box)
{
  glUseProgram(0);
  glLineWidth(1);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glm::mat4 mx = ui->projectionMatrix * ui->current_viewpoint.vp_matrix;
  glLoadMatrixf(&mx[0][0]);

  glColor3f(1,1,1);

  glBegin(GL_LINES);
  glVertex3f(box.x0, box.y0, box.z0);
  glVertex3f(box.x0, box.y0, box.z1);
  glVertex3f(box.x0, box.y1, box.z0);
  glVertex3f(box.x0, box.y1, box.z1);
  glVertex3f(box.x1, box.y0, box.z0);
  glVertex3f(box.x1, box.y0, box.z1);
  glVertex3f(box.x1, box.y1, box.z0);
  glVertex3f(box.x1, box.y1, box.z1);

  glVertex3f(box.x0, box.y0, box.z0);
  glVertex3f(box.x0, box.y1, box.z0);
  glVertex3f(box.x1, box.y0, box.z0);
  glVertex3f(box.x1, box.y1, box.z0);
  glVertex3f(box.x0, box.y0, box.z1);
  glVertex3f(box.x0, box.y1, box.z1);
  glVertex3f(box.x1, box.y0, box.z1);
  glVertex3f(box.x1, box.y1, box.z1);

  glVertex3f(box.x0, box.y0, box.z0);
  glVertex3f(box.x1, box.y0, box.z0);
  glVertex3f(box.x0, box.y1, box.z0);
  glVertex3f(box.x1, box.y1, box.z0);
  glVertex3f(box.x0, box.y0, box.z1);
  glVertex3f(box.x1, box.y0, box.z1);
  glVertex3f(box.x0, box.y1, box.z1);
  glVertex3f(box.x1, box.y1, box.z1);

  glEnd();

  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void show_axes(UserInterface *ui, glm::mat4 model)
{
  glUseProgram(0);
  glLineWidth(2);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glm::mat4 mx = ui->projectionMatrix * ui->current_viewpoint.vp_matrix * model;
  glLoadMatrixf(&mx[0][0]);

  //glm::mat4 m = ui->projectionMatrix * ui->viewMatrix * model;
  glm::mat4 m(1);
  glm::vec4 origin = m * glm::vec4(0,0,0,1);
  glm::vec4 plus_x = m * glm::vec4(1,0,0,1);
  glm::vec4 plus_y = m * glm::vec4(0,1,0,1);
  glm::vec4 plus_z = m * glm::vec4(0,0,1,1);

  //origin = glm::vec4(0,0,0,1);
  printf("origin (%.3f %.3f %3f)\n", origin[0], origin[1], origin[2]);

  glColor3f(1,0,0);
  glBegin(GL_LINES);
  glVertex3f(origin[0], origin[1], origin[2]);
  glVertex3f(plus_x[0], plus_x[1], plus_x[2]);
  glEnd();

  glColor3f(0,1,0);
  glBegin(GL_LINES);
  glVertex3f(origin[0], origin[1], origin[2]);
  glVertex3f(plus_y[0], plus_y[1], plus_y[2]);
  glEnd();

  glColor3f(0,0,1);
  glBegin(GL_LINES);
  glVertex3f(origin[0], origin[1], origin[2]);
  glVertex3f(plus_z[0], plus_z[1], plus_z[2]);
  glEnd();

  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}


void SimpleEntityHandler::draw(UserInterface *ui, Entity *ent)
{
  static long next_sound = 0x7fffffffffffffff;

  // compute the model matrix
  glm::mat4 model = glm::mat4(1);
  glm::vec3 loc = ent->location;
  float facing = ent->facing;

  if (ui->frameTime < ent->smooth.final_time) {
    float dt = ent->smooth.final_time - ui->frameTime;
    glm::vec3 d = ent->smooth.neg_velocity * dt;
    if (ent->id != 0) {
      printf("smooth move %.3f : <%.3f %.3f %.3f> <%.3f>\n",
             dt, d[0], d[1], d[2],
             ent->smooth.neg_facevel * dt);
    }
    loc += d;
    facing += ent->smooth.neg_facevel * dt;
  }
  if (ui->frameTime > next_sound) {
    next_sound = ui->frameTime + 1500000;
    sound_play(ui, ui->sounds.boink, 1, loc[0], loc[1], loc[2]);
  }

  //loc[2] *= z_scale;    // this is a hack; it should be stored in world coords
  model = glm::translate(model, loc);

  float dt = (ui->frameTime - ui->launchTime) * 1.0e-6;
  float args[] = { facing,                              // Shell
                   (float)(30*sin(10*(dt+0.7))),        // BR
                   (float)(30*sin(10*(dt+1.5))),        // FR
                   (float)(20*sin(10*(dt+2.1))),        // BL
                   (float)(30*sin(3*dt)),               // H
                   (float)(20*sin(10*(dt+2.1))),        // FL
                   0, 0 };
  glBindTexture(GL_TEXTURE_2D, textureId);
  mesh->render(ui, model, &args[0]);
}

EntityHandler *EntityHandler::get(UserInterface *ui,
                                  std::string const& type,
                                  std::string const& subtype)
{
  printf("get handler for \"%s\"\n", type.c_str());
  EntityTypeMap::iterator i = ui->etypes.find(type);
  if (i == ui->etypes.end()) {
    printf("   no such entity type :-(\n");
    return NULL;
  }
  return new SimpleEntityHandler(ui, i->second);
}

void UserInterface::update_entity(wire::entity::EntityInfo const& info)
{
  static const bool verbose = false;
  unsigned uid = info.uid();
  EntityMap::iterator i = entities.find(uid);
  Entity *e;
  bool isnew;
  if (i == entities.end()) {
    printf("new entity #%u\n", uid);
    isnew = true;
    e = newEntityFromWire(this, info);
    entities.insert(EntityMap::value_type(uid,e));
  } else {
    //printf("updated entity #%u\n", uid);
    isnew = false;
    e = i->second;
  }

  if (info.has_type()) {
    std::string type(info.type());
    std::string sub;
    if (info.has_subtype()) {
      sub = info.subtype();
    }
    e->handler = EntityHandler::get(this, type, sub);
    printf(" type=\"%s\"/\"%s\"\n", type.c_str(), sub.c_str());
  }
  unsigned l = info.coords_size();
  bool smooth = false;
  float smooth_t = 1.0;
  float oldadj_dt = 0;
  bool oldadj = false;
  if (!isnew && info.has_duration()) {
    smooth = true;
    smooth_t =  info.duration() * 1e6;
    if (frameTime < e->smooth.final_time) {
      oldadj = true;
      oldadj_dt = e->smooth.final_time - frameTime;
    }
  }

  if (l >= 3) {
    //printf(" posn=%.3f,%.3f,%3f\n", info.coords(0), info.coords(1), info.coords(2));
    
    glm::vec3 newloc = glm::vec3(info.coords(0),
                                 info.coords(1),
                                 info.coords(2));
    if (smooth) {
      // configure a smooth transition
      // figure out where we are already at in terms of a smooth transition
      // -- this ensures smooth motion when we get a new change before
      //    finishing the old one
      glm::vec3 curloc = e->location;
      if (oldadj) {
        curloc += e->smooth.neg_velocity * oldadj_dt;
      }
      e->smooth.neg_velocity = (curloc - newloc) / smooth_t;
    }
    e->location = newloc;
  }
  if (l >= 4) {
    float newfacing = info.coords(3);
    if (smooth) {
      float curfacing = e->facing;
      if (verbose) {
        printf("   curfacing=%.3f\n", curfacing);
      }
      if (oldadj) {
        float df = e->smooth.neg_facevel * oldadj_dt;
        if (verbose) {
          printf("      adjust facing by %.3f due to dt %g\n", df, oldadj_dt);
        }
        curfacing += df;
      }
      e->smooth.neg_facevel = (curfacing - newfacing) / smooth_t;
      if (verbose) {
        printf("#%u going from facing %.3f to %.3f over %g, facevel = %g/sec\n",
               e->id,
               curfacing,
               newfacing,
               smooth_t,
               1e6 * e->smooth.neg_facevel);
      }
    }
    e->facing = newfacing;
    if (verbose) {
      printf("#%u set facing %.3f\n", e->id, e->facing);
    }
  }
  if (smooth) {
    e->smooth.final_time = frameTime + (long)smooth_t;
    if (verbose) {
      printf("   current time = %ld\n", frameTime);
      printf("     final time = %ld\n", e->smooth.final_time);
    }
  } else {
    e->smooth.final_time = 0;
  }
  if (l >= 5) {
    e->tilt = info.coords(4);
  }
}

enum StandardTools current_tool(struct UserInterface *ui)
{
  return (enum StandardTools)(7 - ui->toolSlot);
}

bool allsolid(unsigned char *typecode, size_t len)
{
  for (size_t i=0; i<len; i++) {
    if (typecode[i] == 0) {
      return false;
    }
  }
  return true;
}


void ui_process_place(struct UserInterface *ui)
{
  printf("PLACE %d,%d,%d\n", ui->pick.x, ui->pick.y, ui->pick.z);
  RegionPickIndex px(ui->pick.p_rpi);
  Region *rgn = ui->pick.p_rgn;
  int use_z = rgn->basement + px.hitz();
  int use_h = ui->toolHeight;

  SpanVector const& span(rgn->columns[px.y()][px.x()]);

  int use_x = px.x(), use_y = px.y();
  if (px.face() == PICK_INDEX_FACE_TOP) {
    use_z = rgn->basement + px.z0() + span[px.span()].height;
  } else if (px.face() == PICK_INDEX_FACE_BOTTOM) {
    use_z = rgn->basement + px.z0();
    use_z -= use_h;
  } else {
    switch (px.face()) {
    case 0: hex_sw(&use_x, &use_y); break;
    case 1: hex_se(&use_x, &use_y); break;
    case 2: hex_e(&use_x, &use_y); break;
    case 3: hex_ne(&use_x, &use_y); break;
    case 4: hex_nw(&use_x, &use_y); break;
    case 5: hex_w(&use_x, &use_y); break;
    }
    // if we are placing along a face, adjust the z location to align
    // with the adjoining column
    unsigned char v_type[64];
    use_z -= use_h / 2;
    expandSpan(rgn, span, use_z-32, sizeof(v_type), &v_type[0], NULL);
    int max_adj = (use_h < 20) ? use_h : 20;
    for (int adj=0; adj<=((2*max_adj)+1); adj++) {
      int dz = (adj == 0) ? 0 : ((adj & 1) ? -((adj-1)/2) : (adj-1)/2);
      if (allsolid(&v_type[32+dz], use_h)) {
        use_z += dz;
        break;
      }
    }
  }

  printf(" ==> %#lx (%d %d %d %d %d %d) use %d,%d  %d\n",
         ui->pick.p_rpi,
         px.x(), px.y(), px.z0() + rgn->basement, 
         px.face(), px.span(), px.hitz() + rgn->basement,
         use_x, use_y, use_z);

  wire::terrain::Edit e;
  wire::terrain::EditSpan *es = e.add_span();
  es->set_x(rgn->origin.x + use_x);
  es->set_y(rgn->origin.y + use_y);
  es->set_z0(use_z);
  es->set_height(use_h);
  es->set_type(ui->placeToolType);
  std::string buf;
  e.SerializeToString(&buf);
  ui->cnx->send(wire::major::Major::TERRAIN_EDIT, buf);
}

void ui_process_dig(struct UserInterface *ui)
{
  // blow something up
  printf("DIG %d,%d,%d\n", ui->pick.x, ui->pick.y, ui->pick.z);
  RegionPickIndex px(ui->pick.p_rpi);
  Region *rgn = ui->pick.p_rgn;
  int use_z = rgn->basement + px.hitz();
  int use_h = 1;

  if (px.face() == PICK_INDEX_FACE_TOP) {
    SpanVector const& span(rgn->columns[px.y()][px.x()]);
    use_z = rgn->basement + px.z0() + span[px.span()].height;
    use_z -= use_h;
  } else if (px.face() == PICK_INDEX_FACE_BOTTOM) {
    use_z = rgn->basement + px.z0();
  }
  printf(" ==> %#lx (%d %d %d %d %d %d) use %d\n",
         ui->pick.p_rpi,
         px.x(), px.y(), px.z0() + rgn->basement, 
         px.face(), px.span(), px.hitz() + rgn->basement,
         use_z);

  wire::terrain::Edit e;
  wire::terrain::EditSpan *es = e.add_span();
  es->set_x(rgn->origin.x + px.x());
  es->set_y(rgn->origin.y + px.y());
  es->set_z0(use_z);
  es->set_height(use_h);
  es->set_type(0);
  std::string buf;
  e.SerializeToString(&buf);
  ui->cnx->send(wire::major::Major::TERRAIN_EDIT, buf);
}

void ui_process_select(UserInterface *ui)
{
  float range = 10;
  ui->pick.entity = ~0U;
  glm::vec3 pick(hex_x(ui->pick.x, ui->pick.y),
                 hex_y(ui->pick.x, ui->pick.y),
                 z_scale * ui->pick.z);
  Entity *e = NULL;
  for (EntityMap::iterator i=ui->entities.begin();
       i != ui->entities.end();
       ++i) {
    float dist = glm::distance2(i->second->location, pick);
    if (dist < range) {
      range = dist;
      e = i->second;
      ui->pick.entity = i->second->id;
    }
  }
  printf("    PICKED ENTITY #%u at %.3f %.3f %3f\n", 
         ui->pick.entity,
         e->location.x, e->location.y, e->location.z);
}

void ui_process_event(struct UserInterface *ui)
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_MOUSEWHEEL:
      ui->toolSlot = (ui->toolSlot + event.wheel.y) & 7;
      ui->toolPopup->ow_showtime = ui->frameTime;
      ui->toolPopup->ow_cleartime = ui->frameTime + 2*1000000;
      break;

    case SDL_MOUSEBUTTONDOWN:
      {
        enum StandardTools t = current_tool(ui);
        switch (t) {
        case TOOL_DESTROYER:
          if (ui->pick.enable) { ui_process_dig(ui); }
          break;
        case TOOL_BUILDER:
          if (ui->pick.enable) { ui_process_place(ui); }
          break;
        case TOOL_ENTITY_SELECTER:
          ui_process_select(ui);
          break;
        default:
          printf("UNKNOWN TOOL %d\n", t);
        }
      }
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      ui_process_key(ui, &event);
      break;
    case SDL_QUIT:
      ui->done_flag = SDL_TRUE;
      break;
    }
  }
}

void ui_run(struct UserInterface *ui)
{
  GUIWireHandler *g = new GUIWireHandler();
  g->ui = ui;

  while (!ui->done_flag) {
    g->flush_incoming(ui->cnx);
    ui_process_event(ui);

    ui->frame += 1;
    long t0 = real_time();
    double delta = (t0 - ui->frameTime) * 1.0e-6;
    ui->frameTime = t0;
    ui_update(ui, delta);
    ui_render_status(ui);
    ui_render(ui);
    SDL_GL_SwapWindow(ui->window);

    long dt = ui->frameTime - ui->fpsReport.time;
    if (dt > 1000000) {
      long dframes = ui->frame - ui->fpsReport.frame;
      printf("FPS %.3f  at  %.3f %.3f %.3f  facing %.1f\n", (double)dframes / (dt * 1.0e-6),
             ui->location.x,
             ui->location.y,
             ui->location.z,
             ui->facing);
      ui->fpsReport.time = ui->frameTime;
      ui->fpsReport.frame = ui->frame;
      // flush everything every second
      if (ui->playerEntity) {
        ui_flush_player_info(ui);
      } else {
        printf("* no player to flush\n");
      }
    }
    usleep(5000);
  }
}

void ui_close(struct UserInterface *ui)
{
  SDL_GL_DeleteContext(ui->glcontext);
  SDL_DestroyRenderer(ui->renderer);
  SDL_Quit();
}


UserInterface::UserInterface(ClientOptions const& opt)
{
  frame = 0;
  fpsReport.time = 0;
  fpsReport.frame = 0;
  outlineMesh = NULL;
  toolSlot = 0;
  toolHeight = 6;
  placeToolType = 3;

  spanOnTopOf.height = 1;
  spanOnTopOf.type = 1;
  spanOnTopOf.flags = 0;

  activePlayerAnimae = 0;
  playerEntity = NULL;
  currentRegionPosn = Posn(-9999999, -9999999);

  vp_adj_eye.anim = NULL;
  vp_adj_eye.start_time = 0;
  vp_adj_eye.end_time = 0;
  vp_adj_eye.time_scale = 0;

  need_settle = false;
  focus = true;
  done_flag = 0;
  mouseScale = 40.0;
  walkSpeed = 3.141;
  walkingTime = 0;
  eyeHeight = STANDING_EYE_HEIGHT;
  frameTime = real_time();
  launchTime = frameTime;
  solarTimeReference = frameTime;
  solarTimeBase = 0.0;

  //ui->location = glm::vec3(-0.189, -0.771, 0.449);
  //ui->location = glm::vec3(-1.422, -2.551, 1.573);
  //location = glm::vec3(5.316, 5.456, 0);
  location = glm::vec3(1, 1, 0);
  facing = 0/*55.284*/;
  tilt = 0/*-27.428*/;
  pick.entity = ~0;

  status_window = NULL;
  status_renderer = NULL;

  window = SDL_CreateWindow("Main Game",
                            100 /*SDL_WINDOWPOS_UNDEFINED*/,
                            100 /*SDL_WINDOWPOS_UNDEFINED*/,
                            800, 600,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  if (!window) {
    fatal("SDL_CreateWindow", SDL_GetError());
  }


  renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) {
    fatal("SDL_CreateRenderer", SDL_GetError());
  }

  glcontext = SDL_GL_CreateContext(window);
  if (!glcontext) {
    fatal("SDL_GL_CreateContext", SDL_GetError());
  }

  /* Initialize OpenGL stuff */

  struct ShaderRef sr;

  sr.shaderId = load_shader_program("shaders/simple.vertex.glsl",
                                    "shaders/simple.frag.glsl");
  sr.shaderPgmMVPMatrixIndex = glGetUniformLocation(sr.shaderId, "MVP");
  sr.shaderPgmTextureIndex = glGetUniformLocation(sr.shaderId, "theTextureSampler");
  sr.fogColorIndex = glGetUniformLocation(sr.shaderId, "fogColor");
  sr.fogDensityIndex = glGetUniformLocation(sr.shaderId, "fogDensity");
  sr.waterWiggleIndex = 0;
  terrainShader = sr;


  sr.shaderId = load_shader_program("shaders/water.vertex.glsl",
                                    "shaders/water.frag.glsl");
  sr.shaderPgmMVPMatrixIndex = glGetUniformLocation(sr.shaderId, "MVP");
  sr.shaderPgmTextureIndex = glGetUniformLocation(sr.shaderId, "theTextureSampler");
  sr.fogColorIndex = glGetUniformLocation(sr.shaderId, "fogColor");
  sr.fogDensityIndex = glGetUniformLocation(sr.shaderId, "fogDensity");
  sr.waterWiggleIndex = glGetUniformLocation(sr.shaderId, "waterWiggle");
  waterShader = sr;

  memset(&sr, 0, sizeof(sr));
  sr.shaderId = load_shader_program("shaders/skybox.vertex.glsl",
                                    "shaders/skybox.frag.glsl");
  sr.shaderPgmTextureIndex = glGetUniformLocation(sr.shaderId, "cubemap");
  sr.sunPositionIndex = glGetUniformLocation(sr.shaderId, "sun");
  sr.starMatrixIndex = glGetUniformLocation(sr.shaderId, "stars");
  skyboxShader = sr;
  

  sr.shaderId = load_shader_program("shaders/robot.vertex.glsl",
                                    "shaders/robot.frag.glsl");
  sr.shaderPgmMVPMatrixIndex = glGetUniformLocation(sr.shaderId, "MVP");
  sr.shaderPgmTextureIndex = glGetUniformLocation(sr.shaderId, "theTextureSampler");
  sr.fogColorIndex = 0;
  sr.fogDensityIndex = 0;
  sr.waterWiggleIndex = 0;
  robotShader = sr;

  outlineShader.shaderId = load_shader_program("shaders/outline.vertex.glsl",
                                               "shaders/outline.frag.glsl");
  outlineShader.shaderPgmMVPMatrixIndex = glGetUniformLocation(outlineShader.shaderId, "MVP");
  outlineShader.shaderPgmTextureIndex = 0;
  outlineShader.fogColorIndex = 0;
  outlineShader.fogDensityIndex = 0;

  //mainmesh = NULL;
  //build_mesh(this);

  // Projection matrix
  // Projection matrix : 45° Field of View, 4:3 ratio, display range : 0.1 unit <-> 100 units
  projectionMatrix = glm::perspective(45.0f, 4.0f / 3.0f, 0.1f, 100.0f);

  textureId = ui_load_textures("textures/terrain.png");
  cursorTexture = ui_load_textures("textures/turtle.png");
  popupFont = Picture::load_png("textures/8x8font.png");

  cube_map_texture_id = ui_skymap_create(opt, 0.0);
  outlineMesh = NULL;

  popupTexture = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   TEXT_POPUP_W, TEXT_POPUP_H);
  SDL_SetTextureBlendMode(popupTexture, SDL_BLENDMODE_BLEND);

  ui_inputbox_init(this);

  toolPopup = ui_load_overlay(this,
                              "textures/toolbar.png",
                              -0.9, 0.9);
  toolPopupSel = ui_load_overlay(this,
                                 "textures/toolsel.png",
                                 -0.9, 0.9);

  toolPopupSel->ow_showtime = 1;
  toolPopupSel->ow_cleartime = ~0ULL >> 1;
}


Curve *make_hop_curve()
{
  float hop[64];
  for (int i=0; i<64; i++) {
    hop[i] = (cos((63-i)*PI/63.0) - 1)/-2.0;
  }
  return Curve::build(0.0, 1.0, 64, &hop[0]);
}


int main(int argc, char *argv[])
{
  ClientOptions opt;

  if (!opt.parseCommandLine(argc, argv)) {
    return 1;
  }
  if (!opt.parseConfigFile()) {
    return 1;
  }
  if (!opt.validate()) {
    return 1;
  }

  // First thing, change to the home directory
  // in the default ubuntu build, this is configured as
  // "/usr/share/games/hexplore"
  if (chdir(opt.homedir.c_str()) < 0) {
    perror(opt.homedir.c_str());
    return 1;
  }

  hop_up = make_hop_curve();
  ClientWorld *w = create_client_world(opt);
  Connection *cnx = connection_make(w, opt);
  
  if (!cnx) {
    fprintf(stderr, "could not connect to server\n");
    return 1;
  }

  if (getenv("DISPLAY")) {

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO /*EVERYTHING*/) < 0) {
      fatal("SDL_Init", SDL_GetError());
    }

    struct UserInterface *ui = new UserInterface(opt);

    sound_init(ui);
    ui->sounds.boink = sound_load("sounds/boing.wav");

    ui->cnx = cnx;
    ui->world = w;

    printf("run...\n");
    ui_run(ui);
    printf("OK, fine\n");
    ui_close(ui);
  } else {
    tui_run(cnx, w);
  }
  return 0;
}

void tell_entity(UserInterface *ui, std::string const& msg)
{
  wire::entity::Tell tell;
  printf("tell to <#%u>\n", ui->pick.entity);
  if (ui->pick.entity != ~0U) {
    tell.set_target(ui->pick.entity);
    tell.set_message(msg);
  
    std::string buf;
    tell.SerializeToString(&buf);
    ui->cnx->send(wire::major::Major::ENTITY_TELL, buf);
  }
}

int Entity::pick(glm::vec3 const& origin,
                 glm::vec3 const& direction,
                 PickPoint *pickat)
{
  if (handler) {
    return handler->pick(origin, direction, pickat);
  }
  return -1;
}

frect Entity::bbox()
{
  if (handler) {
    frect b = handler->bbox;
    //glm::mat4 model(1);
    b.x0 += location[0];
    b.y0 += location[1];
    b.z0 += location[2];
    b.x1 += location[0];
    b.y1 += location[1];
    b.z1 += location[2];
    return b;
  }
  return frect::empty;
}
