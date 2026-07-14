#ifndef EZ_TEXTURES_H
#define EZ_TEXTURES_H

#include <string>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#ifdef EZREMOTE_ENABLE_OPENGL
#include <GL/gl.h>
#else
#endif

typedef struct {
#ifdef EZREMOTE_ENABLE_OPENGL
    GLuint id;
#else
    SDL_Texture *id;
#endif
    int width;
    int height;
} Tex;

namespace Textures {
    bool LoadImageFile(const std::string filename, Tex *texture);
#ifdef EZREMOTE_ENABLE_OPENGL
    void Init(void);
#else
    void Init(SDL_Renderer *renderer);
#endif
    void Exit(void);
    void Free(Tex *texture);
}

#endif
