#include "overlay.h"
#include <string.h>

static inline uint32_t *alloc_texture(unsigned w, unsigned h, GLuint *id)
{
  glGenTextures(1, id);
  // hard coded as RGBA8
  size_t bytes = w * h * sizeof(uint32_t);

  uint32_t *buf = (uint32_t*)malloc(bytes);
  memset(buf, 0, bytes);
  return buf;
}

OverlayImage::OverlayImage(unsigned w, unsigned h)
  : width(w),
    height(h)
{
  pixelBuffer = alloc_texture(w, h, &textureId);
}


OverlayImage::OverlayImage(Picture *src)
  : width(src->width),
    height(src->height)
{
  pixelBuffer = alloc_texture(width, height, &textureId);
  paint(*src);
  flush();
}

void OverlayImage::flush()
{
  printf("flush overlay texture %u\n", textureId);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
               width,
               height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               pixelBuffer);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  //glGenerateMipmap(GL_TEXTURE_2D);
}

void OverlayImage::paint(Picture const& src, int x, int y)
{
  uint16_t *red_ptr = src.red_plane();
  uint16_t *green_ptr = src.green_plane();
  uint16_t *blue_ptr = src.blue_plane();
  uint16_t *alpha_ptr = src.alpha_plane();
  printf("painting %u x %u at %d,%d  %p ...\n", src.width, src.height, x, y, red_ptr);

  unsigned i = 0;
  unsigned n = 0;
  for (unsigned dy=0; dy<src.height; dy++) {
    if (((y+dy) >= 0) && ((y+dy) < height)) {
      uint32_t *row_base = &pixelBuffer[width * (y + dy) + x];
      for (unsigned dx=0; dx<src.width; dx++) {
        if (((x + dx) >= 0) && ((x + dx) < width)) {
          uint8_t r = red_ptr[i];
          uint8_t g = green_ptr[i];
          uint8_t b = blue_ptr[i];
          uint8_t a = alpha_ptr[i];

          row_base[dx] = (a << 24) | (b << 16) | (g << 8) | r;
          n++;
        }
        i++;
      }
    }
  }
  printf("   painted %u pixels\n", n);
}

Font::Font(Picture *charmap)
{
  // set the width of all the characters to 8
  memset(&f_charwidth[0], 8, sizeof(f_charwidth));

  // load the bitmap from the picture
  uint16_t *red = charmap->red_plane();
  unsigned stride = charmap->width;

  for (int ch=0; ch<256; ch++) {
    uint64_t w = 0;
    for (int dy=0; dy<8; dy++) {
      for (int dx=0; dx<8; dx++) {
        if (red[dx + ch * 8 + dy * stride] < 0x8000) {
          w |= 1;
        }
        w <<= 1;
      }
    }
    f_bits[ch] = w;
  }
}

void OverlayImage::write(Font const& font, std::string const& text, int x, int y)
{
  for (size_t i=0; i<text.size(); i++) {
    uint8_t ch = text[i];
    uint64_t bits = font.f_bits[ch];

    for (int dy=0; dy<8; dy++) {
      if (bits & 0xFF00000000000000UL) {
        for (int dx=0; dx<8; dx++) {
          if (bits & 0x8000000000000000UL) {
            put_pixel(x + dx + i*8,
                      y + dy,
                      0xFF000000);
          }
          bits <<= 1;
        }
      } else {
        bits <<= 8;
      }
    }
  }
}

