#undef main

#include <sstream>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
// #include <dbglogger.h>

#include "imgui.h"
#include "SDL2/SDL.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_sdlrenderer.h"
#include "server/http_server.h"
#include "config.h"
#include "lang.h"
#include "gui.h"
#include "installer.h"
#include "util.h"
#include "textures.h"

extern "C"
{
	static int g_libnetMemId  = -1;
	int sceNetInit();
	int sceNetPoolCreate(const char*, int, int);
	int sceNetPoolDestroy(int);	
};

#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#define NET_HEAP_SIZE   (5 * 1024 * 1024)

// SDL window and software renderer
SDL_Window *window;
SDL_Renderer *renderer;

ImVec4 ColorFromBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f);
};

void InitImgui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	io.Fonts->Clear();
	io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;

	static const ImWchar ranges[] = { // All languages with chinese included
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x0100, 0x024F, // Latin Extended
		0x0370, 0x03FF, // Greek
		0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
		0x0590, 0x05FF, // Hebrew
		0x1E00, 0x1EFF, // Latin Extended Additional
		0x1F00, 0x1FFF, // Greek Extended
		0x2000, 0x206F, // General Punctuation
		0x2100, 0x214F, // Letterlike Symbols
		0x2460, 0x24FF, // Enclosed Alphanumerics
		0x2DE0, 0x2DFF, // Cyrillic Extended-A
		0x2E80, 0x2EFF, // CJK Radicals Supplement
		0x3000, 0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
		0x31F0, 0x31FF, // Katakana Phonetic Extensions
		0x3400, 0x4DBF, // CJK Rare
		0x4E00, 0x9FFF, // CJK Ideograms
		0xA640, 0xA69F, // Cyrillic Extended-B
		0xF900, 0xFAFF, // CJK Compatibility Ideographs
		0xFF00, 0xFFEF, // Half-width characters
		0,
	};

	static const ImWchar arabic[] = { // Arabic
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x0100, 0x024F, // Latin Extended
		0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
		0x1E00, 0x1EFF, // Latin Extended Additional
		0x2000, 0x206F, // General Punctuation
		0x2100, 0x214F, // Letterlike Symbols
		0x2460, 0x24FF, // Enclosed Alphanumerics
		0x0600, 0x06FF, // Arabic
		0x0750, 0x077F, // Arabic Supplement
		0x0870, 0x089F, // Arabic Extended-B
		0x08A0, 0x08FF, // Arabic Extended-A
		0xFB50, 0xFDFF, // Arabic Presentation Forms-A
		0xFE70, 0xFEFF, // Arabic Presentation Forms-B
		0,
	};

	static const ImWchar fa_icons[] {
		0xF07B, 0xF07B, // folder
		0xF65E, 0xF65E, // new folder
		0xF15B, 0xF15B, // file
		0xF021, 0xF021, // refresh
		0xF0CA, 0xF0CA, // select all
		0xF0C9, 0xF0C9, // unselect all
		0x2700, 0x2700, // cut
		0xF0C5, 0xF0C5, // copy
		0xF0EA, 0xF0EA, // paste
		0xF31C, 0xF31C, // edit
		0xE0AC, 0xE0AC, // rename
		0xE5A1, 0xE5A1, // delete
		0xF002, 0xF002, // search
		0xF013, 0xF013, // settings
		0xF0ED, 0xF0ED, // download
		0xF0EE, 0xF0EE, // upload
		0xF56E, 0xF56E, // extract
		0xF56F, 0xF56F, // compress
		0xF0F6, 0xF0F6, // properties
		0xF112, 0xF112, // cancel
		0xF0DA, 0xF0DA, // arrow right
		0x0031, 0x0031, // 1
		0x004C, 0x004C, // L
		0x0052, 0x0052, // R
		0,
	};

	static const ImWchar of_icons[] {
		0xE0CB, 0xE0CB, // square
		0xE0DE, 0xE0DE, // triangle
		0,
	};

	std::string lang = std::string(language);
	int32_t lang_idx = 0;
	//sceSystemServiceParamGetInt( ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG, &lang_idx );

	lang = Util::Trim(lang, " ");
	//bool use_system_lang = lang.empty() || lang.compare("Default") == 0;

	if (lang.compare("Korean") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_KOREAN))
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, NULL, io.Fonts->GetGlyphRangesKorean());
	}
	else if (lang.compare("Simplified Chinese") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_CHINESE_S))
	{
		ImFontConfig config;
		config.OversampleH = 1;
		config.OversampleV = 1;
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
	}
	else if (lang.compare("Traditional Chinese") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_CHINESE_T))
	{
		ImFontConfig config;
		config.OversampleH = 1;
		config.OversampleV = 1;
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
	}
	else if (lang.compare("Japanese") == 0) // || lang.compare("Ryukyuan") == 0 || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_JAPANESE))
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	}
	else if (lang.compare("Thai") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_THAI))
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, NULL, io.Fonts->GetGlyphRangesThai());
	}
	else if (lang.compare("Vietnamese") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_VIETNAMESE))
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, NULL, io.Fonts->GetGlyphRangesVietnamese());
	}
	else if (lang.compare("Greek") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_GREEK))
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, NULL, io.Fonts->GetGlyphRangesGreek());
	}
	else if (lang.compare("Arabic") == 0) // || (use_system_lang && lang_idx == ORBIS_SYSTEM_PARAM_LANG_ARABIC))
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto_ext.ttf", 26.0f, NULL, arabic);
	}
	else
	{
		io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/Roboto.ttf", 26.0f, NULL, ranges);
	}
	ImFontConfig config;
	config.MergeMode = true;
	config.GlyphMinAdvanceX = 13.0f; // Use if you want to make the icon monospaced
	io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/fa-solid-900.ttf", 20.0f, &config, fa_icons);
	io.Fonts->AddFontFromFileTTF("/data/homebrew/ezremote-client/assets/fonts/OpenFontIcons.ttf", 20.0f, &config, of_icons);
	io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
	io.Fonts->Build();

	Lang::SetTranslation(lang_idx);

	auto &style = ImGui::GetStyle();
	style.AntiAliasedLinesUseTex = false;
	style.AntiAliasedLines = true;
	style.AntiAliasedFill = true;
	style.WindowRounding = 1.0f;
	style.FrameRounding = 2.0f;
	style.GrabRounding = 2.0f;

	ImVec4* colors = style.Colors;
	const ImVec4 bgColor           = ColorFromBytes(37, 37, 38);
	const ImVec4 bgColorBlur       = ColorFromBytes(37, 37, 38, 170);
	const ImVec4 lightBgColor      = ColorFromBytes(82, 82, 85);
	const ImVec4 veryLightBgColor  = ColorFromBytes(90, 90, 95);

	const ImVec4 titleColor        = ColorFromBytes(10, 100, 142);
	const ImVec4 panelColor        = ColorFromBytes(51, 51, 55);
	const ImVec4 panelHoverColor   = ColorFromBytes(29, 151, 236);
	const ImVec4 panelActiveColor  = ColorFromBytes(0, 119, 200);

	const ImVec4 textColor         = ColorFromBytes(255, 255, 255);
	const ImVec4 textDisabledColor = ColorFromBytes(151, 151, 151);
	const ImVec4 borderColor       = ColorFromBytes(78, 78, 78);

	colors[ImGuiCol_Text]                 = textColor;
	colors[ImGuiCol_TextDisabled]         = textDisabledColor;
	colors[ImGuiCol_TextSelectedBg]       = panelActiveColor;
	colors[ImGuiCol_WindowBg]             = bgColor;
	colors[ImGuiCol_ChildBg]              = panelColor;
	colors[ImGuiCol_PopupBg]              = bgColor;
	colors[ImGuiCol_Border]               = borderColor;
	colors[ImGuiCol_BorderShadow]         = borderColor;
	colors[ImGuiCol_FrameBg]              = panelColor;
	colors[ImGuiCol_FrameBgHovered]       = panelHoverColor;
	colors[ImGuiCol_FrameBgActive]        = panelActiveColor;
	colors[ImGuiCol_TitleBg]              = titleColor;
	colors[ImGuiCol_TitleBgActive]        = titleColor;
	colors[ImGuiCol_TitleBgCollapsed]     = titleColor;
	colors[ImGuiCol_MenuBarBg]            = panelColor;
	colors[ImGuiCol_ScrollbarBg]          = panelColor;
	colors[ImGuiCol_ScrollbarGrab]        = lightBgColor;
	colors[ImGuiCol_ScrollbarGrabHovered] = veryLightBgColor;
	colors[ImGuiCol_ScrollbarGrabActive]  = veryLightBgColor;
	colors[ImGuiCol_CheckMark]            = panelActiveColor;
	colors[ImGuiCol_SliderGrab]           = panelHoverColor;
	colors[ImGuiCol_SliderGrabActive]     = panelActiveColor;
	colors[ImGuiCol_Button]               = panelColor;
	colors[ImGuiCol_ButtonHovered]        = panelHoverColor;
	colors[ImGuiCol_ButtonActive]         = panelHoverColor;
	colors[ImGuiCol_Header]               = panelColor;
	colors[ImGuiCol_HeaderHovered]        = panelHoverColor;
	colors[ImGuiCol_HeaderActive]         = panelActiveColor;
	colors[ImGuiCol_Separator]            = borderColor;
	colors[ImGuiCol_SeparatorHovered]     = borderColor;
	colors[ImGuiCol_SeparatorActive]      = borderColor;
	colors[ImGuiCol_ResizeGrip]           = bgColor;
	colors[ImGuiCol_ResizeGripHovered]    = panelColor;
	colors[ImGuiCol_ResizeGripActive]     = lightBgColor;
	colors[ImGuiCol_PlotLines]            = panelActiveColor;
	colors[ImGuiCol_PlotLinesHovered]     = panelHoverColor;
	colors[ImGuiCol_PlotHistogram]        = panelActiveColor;
	colors[ImGuiCol_PlotHistogramHovered] = panelHoverColor;
	colors[ImGuiCol_ModalWindowDimBg]     = bgColorBlur;
	colors[ImGuiCol_DragDropTarget]       = bgColor;
	colors[ImGuiCol_NavHighlight]         = titleColor;
	colors[ImGuiCol_Tab]                  = bgColor;
	colors[ImGuiCol_TabActive]            = panelActiveColor;
	colors[ImGuiCol_TabUnfocused]         = bgColor;
	colors[ImGuiCol_TabUnfocusedActive]   = panelActiveColor;
	colors[ImGuiCol_TabHovered]           = panelHoverColor;
}

static void terminate()
{
	if (g_libnetMemId != -1)
	{
		sceNetPoolDestroy(g_libnetMemId);
	}
}

int main()
{
	// dbglogger_init();
	// dbglogger_log("If you see this you've set up dbglogger correctly.");

	// No buffering
	setvbuf(stdout, NULL, _IONBF, 0);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
	{
		return -1;
	}

	if (sceNetInit() != 0) return -1;
	if ((g_libnetMemId=sceNetPoolCreate("ezremote_client", NET_HEAP_SIZE, 0)) < 0)
	{
		return -1;
	}

	CONFIG::LoadConfig();
	HttpServer::Start();
	INSTALLER::StartDirectPackageInstaller();

	// Create a window context
	window = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, FRAME_WIDTH, FRAME_HEIGHT, 0);
	if (window == NULL)
		return 0;

	renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_SOFTWARE);
	if (renderer == NULL)
		return 0;

	Textures::Init(renderer);
	InitImgui();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer_Init(renderer);
	ImGui_ImplSDLRenderer_CreateFontsTexture();

	atexit(terminate);

	GUI::RenderLoop(renderer);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	Textures::Exit();

	ImGui::DestroyContext();

	return 0;
}
