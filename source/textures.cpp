#include "textures.h"

#ifndef EZREMOTE_ENABLE_OPENGL
static SDL_Renderer *renderer;
#endif

namespace Textures {

	bool LoadImageFile(const std::string filename, Tex *texture)
	{
		// Load image from file
		SDL_Surface *image = IMG_Load(filename.c_str());
		if (image == nullptr)
			return false;

		// Convert to RGBA so the pixel layout is always predictable
		SDL_Surface *formatted = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(image);
		if (formatted == nullptr)
			return false;

#ifdef EZREMOTE_ENABLE_OPENGL
		// Upload to OpenGL texture
		GLuint tex_id = 0;
		glGenTextures(1, &tex_id);
		glBindTexture(GL_TEXTURE_2D, tex_id);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		// SDL_PIXELFORMAT_RGBA32 is RGBA byte-order on all platforms
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		             formatted->w, formatted->h, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE,
		             formatted->pixels);

		glBindTexture(GL_TEXTURE_2D, 0);

		texture->id     = tex_id;
#else
		SDL_Texture *sdl_texture = SDL_CreateTextureFromSurface(renderer, formatted);
		if (sdl_texture == nullptr)
		{
			SDL_FreeSurface(formatted);
			return false;
		}
		texture->id = sdl_texture;

#endif
		texture->width  = formatted->w;
		texture->height = formatted->h;

		SDL_FreeSurface(formatted);
		return true;
	}

#ifdef EZREMOTE_ENABLE_OPENGL
	void Init(void)
#else
	void Init(SDL_Renderer *p_renderer)
#endif
	{
#ifndef EZREMOTE_ENABLE_OPENGL
		renderer = p_renderer;
#endif
		IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
	}

	void Exit(void)
	{
		IMG_Quit();
	}

	void Free(Tex *texture)
	{
#ifdef EZREMOTE_ENABLE_OPENGL
		if (texture->id != 0)
		{
			glDeleteTextures(1, &texture->id);
			texture->id = 0;
		}
#else
		if (texture->id != nullptr)
		{
			SDL_DestroyTexture(texture->id);
			texture->id = nullptr;
		}
#endif
	}

}
