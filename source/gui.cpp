#include "imgui.h"
#include <stdio.h>
#include "windows.h"
#include "gui.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl2.h"

bool done = false;
int gui_mode = GUI_MODE_BROWSER;

namespace GUI
{
	int RenderLoop(SDL_Window *window)
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
				ImGui_ImplOpenGL2_NewFrame();
				ImGui_ImplSDL2_NewFrame();

				ImGui::NewFrame();

				Windows::HandleWindowInput();
				Windows::MainWindow();
				Windows::ExecuteActions();

				ImGui::Render();
				glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
				glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
				SDL_GL_SwapWindow(window);
			}
			else if (gui_mode == GUI_MODE_IME)
			{
				Windows::HandleImeInput();
			}
		}
		return 0;
	}
}
