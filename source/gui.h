#ifndef EZ_GUI_H
#define EZ_GUI_H

#include <string>
#include "SDL2/SDL.h"

#define GUI_MODE_BROWSER 0
#define GUI_MODE_IME 1

extern bool done;
extern int gui_mode;

namespace GUI
{
#ifdef EZREMOTE_ENABLE_OPENGL
    int RenderLoop(SDL_Window *window);
#else
    int RenderLoop(SDL_Renderer *renderer);
#endif
}

#endif
