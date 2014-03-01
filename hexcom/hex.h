#ifndef _H_HEXCOM_HEX
#define _H_HEXCOM_HEX

#include <math.h>
#include "hextypes.h"

#define PI (3.1415926535897932385)
#define DEG_TO_RAD(x)  (x * (PI/180.0))
#define RAD_TO_DEG(x)  (x * (180.0/PI))

static const double z_scale = 0.1;

static const double x_stride = 1.0;
static const double a = x_stride/sqrt(12.0);
static const double edge = 2*a;
static const double y_stride = 1.5 * edge;

static const double uv_scale = 1.0 / (4*a);

void convert_xy_to_hex(double x, double y, int *ixp, int *iyp);
void convert_xyz_to_hex(glm::vec3 const &loc, int *ixp, int *iyp, int *izp);

/* these are the origin functions */
static inline double hex_x(int x, int y)
{
  double wx = x * x_stride;
  return (y & 1) ? (wx + x_stride/2) : wx;
}

static inline double hex_y(int x, int y)
{
  return y * y_stride;
}

/* location of hex center, relative to what's returned
   by hex_x() and hex_y()
*/

static inline double hex_center_x(int x, int y)
{
  double wx = x * x_stride;
  return (y & 1) ? (wx + x_stride) : (wx + x_stride/2);
}

static inline double hex_center_y(int x, int y)
{
  return y * y_stride + a;
}

static inline void hex_se(int *x, int *y)
{
  if (*y & 1) { (*x)++; }
  (*y)--;
}

static inline void hex_sw(int *x, int *y)
{
  if (!(*y & 1)) { (*x)--; }
  (*y)--;
}

static inline void hex_ne(int *x, int *y)
{
  if (*y & 1) { (*x)++; }
  (*y)++;
}

static inline void hex_nw(int *x, int *y)
{
  if (!(*y & 1)) { (*x)--; }
  (*y)++;
}

static inline void hex_w(int *x, int *y)
{
  (*x)--;
}

static inline void hex_e(int *x, int *y)
{
  (*x)++;
}

/**
 *  Grand unified neighbor-finder
 */
static inline void hex_neighbor(int n, int *x, int *y)
{
  switch (n) {
  case 0: hex_sw(x, y); break;
  case 1: hex_se(x, y); break;
  case 2: hex_e(x, y); break;
  case 3: hex_ne(x, y); break;
  case 4: hex_nw(x, y); break;
  case 5: hex_w(x, y); break;
  }
}

struct HexEdge {
  dvec2   online;
  dvec2   dir;
};

extern struct HexEdge hex_edges[6];

#endif /* _H_HEXCOM_HEX */
