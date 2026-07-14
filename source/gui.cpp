#include "imgui.h"
#include <stdio.h>
#include "windows.h"
#include "gui.h"
#include "SDL2/SDL.h"
#include "imgui_impl_sdl.h"
#ifdef EZREMOTE_ENABLE_OPENGL
#include "SDL2/SDL_opengl.h"
#include "imgui_impl_opengl2.h"
#else
#include "imgui_impl_sdlrenderer.h"
#endif
bool done = false;
int gui_mode = GUI_MODE_BROWSER;

namespace GUI
{
#ifdef EZREMOTE_ENABLE_OPENGL
	int RenderLoop(SDL_Window *window)
#else
	int RenderLoop(SDL_Renderer *renderer)
#endif
	{
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		Windows::Init();
		while (!done)
		{
			if (gui_mode == GUI_MODE_BROWSER)
			{
				SDL_Event event;
				while (SDL_PollEvent(&event))
				{
					ImGui_ImplSDL2_ProcessEvent(&event);
				}
				GImGui->GcCompactAll = true;
#ifdef EZREMOTE_ENABLE_OPENGL
				ImGui_ImplOpenGL2_NewFrame();
#else
				ImGui_ImplSDLRenderer_NewFrame();
#endif
				ImGui_ImplSDL2_NewFrame();

				ImGui::NewFrame();

				Windows::HandleWindowInput();
				Windows::MainWindow();
				Windows::ExecuteActions();

				ImGui::Render();
#ifdef EZREMOTE_ENABLE_OPENGL
				glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
				glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
				SDL_GL_SwapWindow(window);
#else
				ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
				SDL_RenderPresent(renderer);
#endif
			}
			else if (gui_mode == GUI_MODE_IME)
			{
				Windows::HandleImeInput();
			}
		}
		return 0;
	}
}
