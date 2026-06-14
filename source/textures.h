#ifndef EZ_TEXTURES_H
#define EZ_TEXTURES_H

#include <string>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <GL/gl.h>

typedef struct {
    GLuint id;
    int width;
    int height;
} Tex;

namespace Textures {
    bool LoadImageFile(const std::string filename, Tex *texture);
    void Init(void);
    void Exit(void);
    void Free(Tex *texture);
}

#endif
