#ifndef _H_HEXPLORE_OVERLAY             // -*-c++-*-
#define _H_HEXPLORE_OVERLAY

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <hexcom/picture.h>
#include <string>

struct Font {
  Font(Picture *charmap);
private:
  uint8_t       f_charwidth[256];
  uint64_t      f_bits[256];
  friend class OverlayImage;
};

/**
 *   An overlay image is essentially just a texture.  It's initial
 *   use is for the tool selector and the text popup, but it
 *   might be used for signs or in-game paintings.
 */

struct OverlayImage {
  unsigned      width, height;
  
  OverlayImage(unsigned w, unsigned h);
  OverlayImage(Picture *src);

  /*
   *  Make local changes to the image
   */
  void paint(Picture const& src, int x=0, int y=0);
  void write(Font const& font, std::string const& text, int x, int y);
  /*
   *  Flush changes back to OpenGL
   */
  void flush();
  /*
   *  Configure it as the current texture
   */
  void bind() {
    glBindTexture(GL_TEXTURE_2D, textureId);
  }
  void unbind() {
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void put_pixel(int x, int y, uint32_t color)
  {
    if ((x < 0) || (y < 0) || ((unsigned)x >= width) || ((unsigned)y >= height)) {
      // clip outside the texture
      return;
    }
    size_t k = x + y * width;
    pixelBuffer[k] = color;
  }

private:
  GLuint        textureId;
  uint32_t      *pixelBuffer;
};

#endif /* _H_HEXPLORE_OVERLAY */
