#include <string.h>
#include "region.h"

#include "spanvec.cpp"

void expandSpan(Region *rgn, SpanVector const& vec, int z_start, int h,
                unsigned char *types,
                unsigned char *flags)
{
  int z0 = rgn->basement;
  int z_limit = z_start + h;
  if (types) {
    memset(types, 0, h);
  }
  if (flags) {
    memset(flags, 0, h);
  }
  for (SpanVector::const_iterator i = vec.begin();
       i != vec.end();
       ++i) {
    int z1 = z0 + i->height;
    if ((z0 < z_limit) && (z1 > z_start)) {
      // there is some overlap
      int j0 = z0 - z_start;
      if (j0 < 0) {
        j0 = 0;
      }
      int j1 = z1 - z_start;
      if (j1 >= h) {
        j1 = h;
      }
      /*printf("span %d:%d  covers %d:%d of the %d/%d expansion\n",
             z0, z1,
             j0, j1,
             z_start, h);*/
      assert((j0 >= 0) && (j0 < h));
      assert((j1 > 0) && (j1 <= h));
      if (types) {
        memset(&types[j0], i->type, (j1-j0));
      }
      if (flags) {
        memset(&flags[j0], i->flags, (j1-j0));
      }
    }
    z0 = z1;
    if (z0 >= z_limit) {
      // early exit if there's more stuff but all above our area of interest
      break;
    }
  }
}

static inline char blocktypechar(uint8_t t)
{
  switch (t) {
  case 0: return ' ';
  case 1: case 2: case 3: case 4: case 5:
  case 6: case 7: case 8: case 9:
    return t + '0';
  case 240: return '~';
  case 255: return '?';
  default: return '#';
  }
}

int Region::set(int x, int y, int z, int height, int type, int flags)
{
  int ix = x - origin.x;
  int iy = y - origin.y;
  int iz = z - basement;

  printf("dset(%d,%d) at %d for %d to %d (flags %d)\n",
         ix, iy, iz, height, type, flags);
  if ((ix < 0) || (ix >= REGION_SIZE) || (iy < 0) || (iy >= REGION_SIZE)) {
    return -1;
  }

  {
    unsigned char buf[64];
    expandSpan(this, columns[iy][ix], z-32, 64, &buf[0], NULL);

    printf("        ");
    for (int i=0; i<64; i++) {
      putchar((i==32) ? '|' : ' ');
    }
    printf("\n");
    printf("BEFORE: ");
    for (int i=0; i<64; i++) {
      putchar(blocktypechar(buf[i]));
    }
    printf("\n");
  }

  Span s;
  s.height = height;
  s.type = type;
  s.flags = flags;

  insert_span(&columns[iy][ix], iz, s);

  {
    unsigned char buf[64];
    expandSpan(this, columns[iy][ix], z-32, 64, &buf[0], NULL);

    printf(" AFTER: ");
    for (int i=0; i<64; i++) {
      putchar(blocktypechar(buf[i]));
    }
    printf("\n");
  }

  return 0;
}
