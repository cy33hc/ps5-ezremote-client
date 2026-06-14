#include "textures.h"

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
		texture->width  = formatted->w;
		texture->height = formatted->h;

		SDL_FreeSurface(formatted);
		return true;
	}

	void Init(void)
	{
		IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
	}

	void Exit(void)
	{
		IMG_Quit();
	}

	void Free(Tex *texture)
	{
		if (texture->id != 0)
		{
			glDeleteTextures(1, &texture->id);
			texture->id = 0;
		}
	}

}
