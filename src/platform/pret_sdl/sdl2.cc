#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#ifdef __PSP__
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspgu.h>
#endif

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "global.h"
#include "core.h"
#include "lib/agb_flash/flash_internal.h"
#include "platform/shared/dma.h"
#include "platform/shared/input.h"
#include "platform/shared/video/gpsp_renderer.h"

#if ENABLE_AUDIO
#include "platform/shared/audio/cgb_audio.h"
#endif

#include "game/globals.h"
#include "game/shared/stage/player.h"
#ifdef __cplusplus
}
#endif

ALIGNED(256) uint16_t gameImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];

#if ENABLE_VRAM_VIEW
uint16_t vramBuffer[VRAM_VIEW_WIDTH * VRAM_VIEW_HEIGHT];
#endif

SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
#if ENABLE_VRAM_VIEW
SDL_Window *vramWindow;
SDL_Renderer *vramRenderer;
SDL_Texture *vramTexture;
#endif
#define INITIAL_VIDEO_SCALE 1
unsigned int videoScale = INITIAL_VIDEO_SCALE;
unsigned int preFullscreenVideoScale = INITIAL_VIDEO_SCALE;

bool speedUp = false;
bool videoScaleChanged = false;
bool isRunning = true;
bool paused = false;
bool stepOneFrame = false;
bool headless = false;

u32 fullScreenFlags = 0;
static SDL_DisplayMode sdlDispMode = { 0 };

bool showMenu = false;
bool cheatGodMode = false;
bool cheatInfiniteRings = false;
bool cheatInfiniteLives = false;
bool cheatMoonJump = false;
float cheatSpeedMultiplier = 5.0f;
bool cheatMuteAudio = false;

struct KeyBind {
    const char* name;
    u16 gbaMask;
    SDL_Keycode keyboardKey;
    SDL_GameControllerButton controllerButton;
};

// Global active bindings database
static KeyBind binds[] = {
    { "A Button", A_BUTTON, SDLK_c, SDL_CONTROLLER_BUTTON_A },
    { "B Button", B_BUTTON, SDLK_x, SDL_CONTROLLER_BUTTON_B },
    { "Select", SELECT_BUTTON, SDLK_BACKSLASH, SDL_CONTROLLER_BUTTON_BACK },
    { "Start", START_BUTTON, SDLK_RETURN, SDL_CONTROLLER_BUTTON_START },
    { "L Shoulder", L_BUTTON, SDLK_s, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
    { "R Shoulder", R_BUTTON, SDLK_d, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    { "D-Pad Up", DPAD_UP, SDLK_UP, SDL_CONTROLLER_BUTTON_DPAD_UP },
    { "D-Pad Down", DPAD_DOWN, SDLK_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
    { "D-Pad Left", DPAD_LEFT, SDLK_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
    { "D-Pad Right", DPAD_RIGHT, SDLK_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT }
};

static int waiting_for_key = -1;
static int waiting_for_controller_btn = -1;
static SDL_GameController* controller = NULL;

static void SaveImGuiSettings(void)
{
    FILE* f = fopen("sa2_settings.cfg", "w");
    if (!f) return;

    fprintf(f, "videoScale=%d\n", videoScale);
    fprintf(f, "fullscreen=%d\n", (fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 1 : 0);
    fprintf(f, "muteAudio=%d\n", cheatMuteAudio ? 1 : 0);
    fprintf(f, "speedMultiplier=%.1f\n", cheatSpeedMultiplier);
    fprintf(f, "godMode=%d\n", cheatGodMode ? 1 : 0);
    fprintf(f, "infiniteRings=%d\n", cheatInfiniteRings ? 1 : 0);
    fprintf(f, "infiniteLives=%d\n", cheatInfiniteLives ? 1 : 0);
    fprintf(f, "moonJump=%d\n", cheatMoonJump ? 1 : 0);

    for (int i = 0; i < 10; i++) {
        fprintf(f, "key_%d=%d\n", i, (int)binds[i].keyboardKey);
        fprintf(f, "btn_%d=%d\n", i, (int)binds[i].controllerButton);
    }

    fclose(f);
}

static void LoadImGuiSettings(void)
{
    FILE* f = fopen("sa2_settings.cfg", "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        char val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (strcmp(key, "videoScale") == 0) {
                videoScale = atoi(val);
                if (videoScale < 1) videoScale = 1;
                if (videoScale > 5) videoScale = 5;
                videoScaleChanged = true;
            } else if (strcmp(key, "fullscreen") == 0) {
                int fs = atoi(val);
                if (fs) {
                    fullScreenFlags = SDL_WINDOW_FULLSCREEN_DESKTOP;
                } else {
                    fullScreenFlags = 0;
                }
            } else if (strcmp(key, "muteAudio") == 0) {
                cheatMuteAudio = atoi(val) != 0;
            } else if (strcmp(key, "speedMultiplier") == 0) {
                cheatSpeedMultiplier = atof(val);
                if (cheatSpeedMultiplier < 1.0f) cheatSpeedMultiplier = 1.0f;
                if (cheatSpeedMultiplier > 10.0f) cheatSpeedMultiplier = 10.0f;
            } else if (strcmp(key, "godMode") == 0) {
                cheatGodMode = atoi(val) != 0;
            } else if (strcmp(key, "infiniteRings") == 0) {
                cheatInfiniteRings = atoi(val) != 0;
            } else if (strcmp(key, "infiniteLives") == 0) {
                cheatInfiniteLives = atoi(val) != 0;
            } else if (strcmp(key, "moonJump") == 0) {
                cheatMoonJump = atoi(val) != 0;
            }

            for (int i = 0; i < 10; i++) {
                char keyName[32];
                snprintf(keyName, sizeof(keyName), "key_%d", i);
                if (strcmp(key, keyName) == 0) {
                    binds[i].keyboardKey = (SDL_Keycode)atoi(val);
                }
                snprintf(keyName, sizeof(keyName), "btn_%d", i);
                if (strcmp(key, keyName) == 0) {
                    binds[i].controllerButton = (SDL_GameControllerButton)atoi(val);
                }
            }
        }
    }
    fclose(f);
}

#ifdef __PSP__
static SDL_Joystick *joystick = NULL;
static SDL_Rect pspDestRect;
#endif

double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
double accumulator = 0.0;

static FILE *sSaveFile = NULL;

#ifdef __cplusplus
extern "C" {
#endif
extern void AgbMain(void);
void DoSoftReset(void);
void VBlankIntrWait(void);
void Platform_StoreSaveFile(void);
u16 Platform_GetKeyInput(void);
void Platform_QueueAudio(const s16 *data, uint32_t bytesCount);
#ifdef __cplusplus
}
#endif
void DoSoftReset(void) {}

void ProcessSDLEvents(void);
void VDraw(SDL_Texture *texture);
void VramDraw(SDL_Texture *texture);

static void ReadSaveFile(const char *path);
static void StoreSaveFile(void);
static void CloseSaveFile(void);

#ifdef _WIN32
void *Platform_malloc(size_t numBytes) { return HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, numBytes); }
void Platform_free(void *ptr) { HeapFree(GetProcessHeap(), 0, ptr); }
#endif

#ifdef __PSP__
PSP_MODULE_INFO("SonicAdvance2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

unsigned int sce_newlib_stack_size = 512 * 1024;

extern bool isRunning;

int exitCallback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    isRunning = false;
    return 0;
}

int callbackThread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int setupPspCallbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, 0);
    }
    return thid;
}
#endif

int main(int argc, char **argv)
{
#ifdef __PSP__
    setupPspCallbacks();
#endif

    const char *headlessEnv = getenv("HEADLESS");

    if (headlessEnv && strcmp(headlessEnv, "true") == 0) {
        headless = true;
    }

    const char *parentEnv = getenv("SIO_PARENT");

    if (parentEnv && strcmp(parentEnv, "true") == 0) {
        SIO_MULTI_CNT->id = 0;
        SIO_MULTI_CNT->si = 1;
        SIO_MULTI_CNT->sd = 1;
        SIO_MULTI_CNT->enable = false;
    }

    // Open an output console on Windows
#if (defined _WIN32) && (DEBUG != 0)
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    freopen("CON", "w", stdout);
#endif

    ReadSaveFile("sa2.sav");

    // Prevent the multiplayer screen from being drawn ( see core.c:EngineInit() )
    REG_RCNT = 0x8000;
    REG_KEYINPUT = 0x3FF;

    if (headless) {
#if ENABLE_AUDIO
        // Required or it makes an infinite loop
        cgb_audio_init(48000);
#endif
        AgbMain();
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    // Open standard game controller if available
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                break;
            }
        }
    }

#ifdef TITLE_BAR
    const char *title = STR(TITLE_BAR);
#else
    const char *title = "SAT-R sa2";
#endif

#ifdef __PSP__
    sdlWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 272, SDL_WINDOW_SHOWN);
#else
    sdlWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale,
                                 DISPLAY_HEIGHT * videoScale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (sdlWindow == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

#if ENABLE_VRAM_VIEW
    int mainWindowX;
    int mainWindowWidth;
    SDL_GetWindowPosition(sdlWindow, &mainWindowX, NULL);
    SDL_GetWindowSize(sdlWindow, &mainWindowWidth, NULL);
    int vramWindowX = mainWindowX + mainWindowWidth;
    u16 vramWindowWidth = VRAM_VIEW_WIDTH;
    u16 vramWindowHeight = VRAM_VIEW_HEIGHT;
    vramWindow = SDL_CreateWindow("VRAM View", vramWindowX, SDL_WINDOWPOS_CENTERED, vramWindowWidth, vramWindowHeight,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (vramWindow == NULL) {
        fprintf(stderr, "VRAM Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#endif

#ifdef __PSP__
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (sdlRenderer == NULL)
        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED);
    if (sdlRenderer == NULL)
        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
#else
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
#endif
    if (sdlRenderer == NULL) {
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

#if ENABLE_VRAM_VIEW
    vramRenderer = SDL_CreateRenderer(vramWindow, -1, SDL_RENDERER_PRESENTVSYNC);
    if (vramRenderer == NULL) {
        fprintf(stderr, "VRAM Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#endif

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#ifdef __PSP__
    // SDL_RenderSetLogicalSize is broken on PSP, stretch to fill manually
    pspDestRect = (SDL_Rect) { 0, 0, GU_SCR_WIDTH, GU_SCR_HEIGHT };
#else
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif
#if ENABLE_VRAM_VIEW
    SDL_SetRenderDrawColor(vramRenderer, 0, 0, 0, 255);
    SDL_RenderClear(vramRenderer);
    SDL_RenderSetLogicalSize(vramRenderer, vramWindowWidth, vramWindowHeight);
#endif

    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (sdlTexture == NULL) {
        fprintf(stderr, "Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

#if ENABLE_VRAM_VIEW
    vramTexture = SDL_CreateTexture(vramRenderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, vramWindowWidth, vramWindowHeight);
    if (vramTexture == NULL) {
        fprintf(stderr, "Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#endif

#if ENABLE_AUDIO
    SDL_AudioSpec want;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 48000;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = (want.freq / 60);
    cgb_audio_init(want.freq);

    if (SDL_OpenAudio(&want, 0) < 0) {
        SDL_Log("Failed to open audio: %s", SDL_GetError());
    } else {
        if (want.format != AUDIO_S16) /* we let this one thing change. */
            SDL_Log("We didn't get S16 audio format.");
        SDL_PauseAudio(0);
    }
#endif

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(sdlWindow, sdlRenderer);
    ImGui_ImplSDLRenderer2_Init(sdlRenderer);

    LoadImGuiSettings();
    if (fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(sdlWindow, fullScreenFlags);
    } else {
        SDL_SetWindowSize(sdlWindow, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale);
        videoScaleChanged = false; 
    }

    VDraw(sdlTexture);
#if ENABLE_VRAM_VIEW
    VramDraw(vramTexture);
#endif
    AgbMain();

    return 0;
}

void DrawImGuiMenu(void)
{
    // Apply persistent cheats
    if (cheatGodMode) {
        gPlayer.timerInvincibility = 100;
        gPlayer.timerInvulnerability = 100;
    }
    if (cheatInfiniteRings) {
        gRingCount = 999;
    }
    if (cheatInfiniteLives) {
        gNumLives = 99;
    }
    if (cheatMoonJump) {
        // Standard GBA Jump (A button) check
        if (gPlayer.heldInput & 0x0001) {
            gPlayer.qSpeedAirY = -1500;
        }
    }
    if (cheatMuteAudio) {
        SDL_PauseAudio(1);
    } else {
        SDL_PauseAudio(0);
    }

    if (speedUp) {
        timeScale = cheatSpeedMultiplier;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    // Style colors for a gorgeous retro-futuristic dark translucent interface
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.12f, 0.18f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.18f, 0.18f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.25f, 0.40f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.38f, 0.60f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.50f, 0.80f, 1.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | 
                             ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("Sonic Advance 2 - Developer Menu", &showMenu, flags)) {
        ImGui::PopStyleColor(6);
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "SONIC ADVANCE 2");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Developer & Options Suite");

    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    if (ImGui::Button("Resume Game", ImVec2(130, 0))) {
        showMenu = false;
    }
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("DevTabBar")) {
        // --- Settings Tab ---
        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Text("Video System");
            ImGui::Separator();

            const char* scales[] = { "1x", "2x", "3x", "4x", "5x" };
            int currentScale = (int)videoScale - 1;
            if (ImGui::Combo("Window Scale", &currentScale, scales, IM_ARRAYSIZE(scales))) {
                videoScale = currentScale + 1;
                videoScaleChanged = true;
                SaveImGuiSettings();
            }

            bool isFullscreen = (fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            if (ImGui::Checkbox("Fullscreen", &isFullscreen)) {
                fullScreenFlags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
                if (fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                    SDL_GetWindowDisplayMode(sdlWindow, &sdlDispMode);
                    preFullscreenVideoScale = videoScale;
                } else {
                    SDL_SetWindowDisplayMode(sdlWindow, &sdlDispMode);
                    videoScale = preFullscreenVideoScale;
                }
                SDL_SetWindowFullscreen(sdlWindow, fullScreenFlags);
                SDL_SetWindowSize(sdlWindow, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale);
                videoScaleChanged = false;
                SaveImGuiSettings();
            }

            ImGui::Spacing();
            ImGui::Text("Audio & Speed");
            ImGui::Separator();

            if (ImGui::Checkbox("Mute Audio Overlay", &cheatMuteAudio)) {
                SaveImGuiSettings();
            }
            if (ImGui::SliderFloat("Speed-up Factor", &cheatSpeedMultiplier, 1.0f, 10.0f, "%.1fx")) {
                SaveImGuiSettings();
            }
            ImGui::TextDisabled("Hold [Spacebar] in-game to fast-forward the engine.");

            ImGui::EndTabItem();
        }

        // --- Cheats Tab ---
        if (ImGui::BeginTabItem("Cheats")) {
            if (ImGui::Checkbox("God Mode (Invincibility)", &cheatGodMode)) {
                SaveImGuiSettings();
            }
            if (ImGui::Checkbox("Infinite Rings (999)", &cheatInfiniteRings)) {
                SaveImGuiSettings();
            }
            if (ImGui::Checkbox("Infinite Lives (99)", &cheatInfiniteLives)) {
                SaveImGuiSettings();
            }
            if (ImGui::Checkbox("Moon Jump (Hold Jump button to fly)", &cheatMoonJump)) {
                SaveImGuiSettings();
            }

            ImGui::Separator();
            ImGui::Text("Level & Coordinate Control");

            if (ImGui::Button("Warp to Start Point")) {
                gPlayer.qWorldX = gPlayer.checkPointX << 8;
                gPlayer.qWorldY = gPlayer.checkPointY << 8;
                gPlayer.qSpeedAirX = 0;
                gPlayer.qSpeedAirY = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Warp Forward (+800px)")) {
                gPlayer.qWorldX += 800 << 8;
            }

            ImGui::EndTabItem();
        }

        // --- Diagnostics Tab ---
        if (ImGui::BeginTabItem("Diagnostics")) {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("Performance");
            ImGui::Separator();
            ImGui::Text("FPS: %.1f (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);

            ImGui::Spacing();
            ImGui::Text("Game Variables");
            ImGui::Separator();
            ImGui::Text("Rings Count: %d", gRingCount);
            ImGui::Text("Lives Count: %d", gNumLives);
            ImGui::Text("Score: %d", gLevelScore);
            ImGui::Text("Current Level ID: %d", gCurrentLevel);

            const char* charNames[] = { "Sonic", "Cream", "Tails", "Knuckles", "Amy" };
            if (gSelectedCharacter >= 0 && gSelectedCharacter < 5) {
                ImGui::Text("Active Character: %s", charNames[gSelectedCharacter]);
            } else {
                ImGui::Text("Active Character ID: %d", gSelectedCharacter);
            }

            ImGui::Spacing();
            ImGui::Text("Physics & Metrics");
            ImGui::Separator();

            float posX = (float)(gPlayer.qWorldX) / 256.0f;
            float posY = (float)(gPlayer.qWorldY) / 256.0f;
            float speedX = (float)(gPlayer.qSpeedAirX) / 256.0f;
            float speedY = (float)(gPlayer.qSpeedAirY) / 256.0f;

            ImGui::Text("Coord X: %.2f (Raw: %d)", posX, gPlayer.qWorldX);
            ImGui::Text("Coord Y: %.2f (Raw: %d)", posY, gPlayer.qWorldY);
            ImGui::Text("Speed X: %.2f (Raw: %d)", speedX, gPlayer.qSpeedAirX);
            ImGui::Text("Speed Y: %.2f (Raw: %d)", speedY, gPlayer.qSpeedAirY);

            ImGui::EndTabItem();
        }

        // --- Controls Tab ---
        if (ImGui::BeginTabItem("Controls")) {
            ImGui::Text("Input Re-binding");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Columns(3, "ControlsColumns", true);
            ImGui::SetColumnWidth(0, 150);
            ImGui::SetColumnWidth(1, 200);
            ImGui::SetColumnWidth(2, 200);

            ImGui::Text("GBA Button"); ImGui::NextColumn();
            ImGui::Text("Keyboard Bind"); ImGui::NextColumn();
            ImGui::Text("Gamepad Button"); ImGui::NextColumn();
            ImGui::Separator();

            for (int i = 0; i < 10; i++) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", binds[i].name); ImGui::NextColumn();

                // Keyboard bind button
                char kbLabel[64];
                if (waiting_for_key == i) {
                    strcpy(kbLabel, "[ Press Key... ]");
                } else {
                    const char* keyName = SDL_GetKeyName(binds[i].keyboardKey);
                    if (keyName && strlen(keyName) > 0) {
                        snprintf(kbLabel, sizeof(kbLabel), "%s", keyName);
                    } else {
                        snprintf(kbLabel, sizeof(kbLabel), "Keycode %d", binds[i].keyboardKey);
                    }
                }

                if (ImGui::Button(kbLabel, ImVec2(180, 0))) {
                    waiting_for_key = i;
                    waiting_for_controller_btn = -1;
                }
                ImGui::NextColumn();

                // Controller bind button
                char padLabel[64];
                if (waiting_for_controller_btn == i) {
                    strcpy(padLabel, "[ Press Button... ]");
                } else {
                    const char* btnName = SDL_GameControllerGetStringForButton(binds[i].controllerButton);
                    if (btnName && strlen(btnName) > 0) {
                        snprintf(padLabel, sizeof(padLabel), "Btn %s", btnName);
                    } else {
                        snprintf(padLabel, sizeof(padLabel), "Button %d", (int)binds[i].controllerButton);
                    }
                }

                if (ImGui::Button(padLabel, ImVec2(180, 0))) {
                    waiting_for_controller_btn = i;
                    waiting_for_key = -1;
                }
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Reset to Defaults", ImVec2(200, 30))) {
                binds[0].keyboardKey = SDLK_c; binds[0].controllerButton = SDL_CONTROLLER_BUTTON_A;
                binds[1].keyboardKey = SDLK_x; binds[1].controllerButton = SDL_CONTROLLER_BUTTON_B;
                binds[2].keyboardKey = SDLK_BACKSLASH; binds[2].controllerButton = SDL_CONTROLLER_BUTTON_BACK;
                binds[3].keyboardKey = SDLK_RETURN; binds[3].controllerButton = SDL_CONTROLLER_BUTTON_START;
                binds[4].keyboardKey = SDLK_s; binds[4].controllerButton = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
                binds[5].keyboardKey = SDLK_d; binds[5].controllerButton = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
                binds[6].keyboardKey = SDLK_UP; binds[6].controllerButton = SDL_CONTROLLER_BUTTON_DPAD_UP;
                binds[7].keyboardKey = SDLK_DOWN; binds[7].controllerButton = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
                binds[8].keyboardKey = SDLK_LEFT; binds[8].controllerButton = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
                binds[9].keyboardKey = SDLK_RIGHT; binds[9].controllerButton = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
                SaveImGuiSettings();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleColor(6);
}

bool newFrameRequested = FALSE;

// called every gba frame. we process sdl events and render as many times
// as vsync needs, then return when a new game frame is needed.
void VBlankIntrWait(void)
{
#define HANDLE_VBLANK_INTRS()                                                                                                              \
    ({                                                                                                                                     \
        REG_DISPSTAT |= INTR_FLAG_VBLANK;                                                                                                  \
        RunDMAs(DMA_VBLANK);                                                                                                               \
        if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR)                                                                                           \
            gIntrTable[INTR_INDEX_VBLANK]();                                                                                               \
        REG_DISPSTAT &= ~INTR_FLAG_VBLANK;                                                                                                 \
    })

    if (headless) {
        REG_VCOUNT = DISPLAY_HEIGHT + 1;
        HANDLE_VBLANK_INTRS();
        return;
    }

    bool frameAvailable = TRUE;
    bool frameDrawn = false;

    while (isRunning) {
#ifndef __PSP__
        ProcessSDLEvents();
#endif

        if (!paused || stepOneFrame) {
            double dt = fixedTimestep / timeScale; // TODO: Fix speedup

            // don't accumulate time if we already requested a new frame
            // this frame cycle (emulates threaded sdl behavior)
            if (!newFrameRequested) {
                double deltaTime = 0;

                curGameTime = SDL_GetPerformanceCounter();
                if (stepOneFrame) {
                    deltaTime = dt;
                } else {
                    deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
                    if (deltaTime > (dt * 5))
                        deltaTime = dt * 5;
                }
                lastGameTime = curGameTime;

                accumulator += deltaTime;
            } else {
                newFrameRequested = FALSE;
            }

            while (accumulator >= dt) {
                REG_KEYINPUT = KEYS_MASK ^ Platform_GetKeyInput();
                if (frameAvailable) {
                    VDraw(sdlTexture);
                    frameAvailable = FALSE;
                    frameDrawn = true;

                    HANDLE_VBLANK_INTRS();

                    accumulator -= dt;
                } else {
                    newFrameRequested = TRUE;
                    return;
                }
            }

            if (paused && stepOneFrame) {
                stepOneFrame = false;
            }
        }

        // present
#ifdef __PSP__
        // manual blit since SDL_RenderSetLogicalSize doesn't work on psp
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &pspDestRect);
        SDL_RenderPresent(sdlRenderer);
#else
        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);

#if ENABLE_VRAM_VIEW
        VramDraw(vramTexture);
        SDL_RenderClear(vramRenderer);
        SDL_RenderCopy(vramRenderer, vramTexture, NULL, NULL);
#endif
        if (videoScaleChanged) {
            SDL_SetWindowSize(sdlWindow, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale);
            if (!(fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
                ImGui::GetIO().DisplaySize = ImVec2((float)(DISPLAY_WIDTH * videoScale), (float)(DISPLAY_HEIGHT * videoScale));
            }
            videoScaleChanged = false;
        }

        if (showMenu) {
            SDL_RenderSetLogicalSize(sdlRenderer, 0, 0);

            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            // Override with raw unscaled window mouse coordinates to ensure 100% precision
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            ImGui::GetIO().MousePos = ImVec2((float)mx, (float)my);

            ImGui::NewFrame();

            DrawImGuiMenu();

            ImGui::Render();
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdlRenderer);

            SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }

        SDL_RenderPresent(sdlRenderer);
#if ENABLE_VRAM_VIEW
        SDL_RenderPresent(vramRenderer);
#endif
#endif
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    CloseSaveFile();

    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
#ifdef __PSP__
    sceKernelExitGame();
#endif
    exit(0);
#undef HANDLE_VBLANK_INTRS
}

static void ReadSaveFile(const char *path)
{
    // Check whether the saveFile exists, and create it if not
    sSaveFile = fopen(path, "r+b");
    if (sSaveFile == NULL) {
        sSaveFile = fopen(path, "w+b");
    }

    fseek(sSaveFile, 0, SEEK_END);
    int fileSize = ftell(sSaveFile);
    fseek(sSaveFile, 0, SEEK_SET);

    // Only read as many bytes as fit inside the buffer
    // or as many bytes as are in the file
    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);

    int bytesRead = fread(FLASH_BASE, 1, bytesToRead, sSaveFile);

    // Fill the buffer if the savefile was just created or smaller than the buffer itself
    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++) {
        FLASH_BASE[i] = 0xFF;
    }
}

static void StoreSaveFile()
{
    if (sSaveFile != NULL) {
        fseek(sSaveFile, 0, SEEK_SET);
        fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), sSaveFile);
    }
}

void Platform_StoreSaveFile(void) { StoreSaveFile(); }

static void CloseSaveFile()
{
    if (sSaveFile != NULL) {
        fclose(sSaveFile);
    }
}

static u16 keys;

static u16 PollControllerButtons(void)
{
    u16 newKeys = 0;
    if (controller == NULL) {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                controller = SDL_GameControllerOpen(i);
                if (controller) break;
            }
        }
    }

    if (controller != NULL) {
        if (!SDL_GameControllerGetAttached(controller)) {
            SDL_GameControllerClose(controller);
            controller = NULL;
        } else {
            for (int i = 0; i < 10; i++) {
                if (SDL_GameControllerGetButton(controller, binds[i].controllerButton)) {
                    newKeys |= binds[i].gbaMask;
                }
            }
        }
    }
    return newKeys;
}

void Platform_QueueAudio(const s16 *data, uint32_t bytesCount)
{
    if (headless) {
        return;
    }
    // Reset the audio buffer if we are 10 frames out of sync
    // If this happens it suggests there was some OS level lag
    // in playing audio. The queue length should remain stable at < 10 otherwise
    if (SDL_GetQueuedAudioSize(1) > (bytesCount * 10)) {
        SDL_ClearQueuedAudio(1);
    }

    SDL_QueueAudio(1, data, bytesCount);
    // printf("Queueing %d\n, QueueSize %d\n", bytesCount, SDL_GetQueuedAudioSize(1));
}

void GetCurrentScaleFactors(float* outScaleX, float* outScaleY)
{
    if (fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        int w, h;
        SDL_GetWindowSize(sdlWindow, &w, &h);
        *outScaleX = (float)w / (float)DISPLAY_WIDTH;
        *outScaleY = (float)h / (float)DISPLAY_HEIGHT;
    } else {
        *outScaleX = (float)videoScale;
        *outScaleY = (float)videoScale;
    }
}

void ProcessSDLEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        SDL_Event imguiEvent = event;
        if (event.type == SDL_MOUSEMOTION) {
            float scaleX, scaleY;
            GetCurrentScaleFactors(&scaleX, &scaleY);
            imguiEvent.motion.x = (int)(event.motion.x * scaleX);
            imguiEvent.motion.y = (int)(event.motion.y * scaleY);
        } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            float scaleX, scaleY;
            GetCurrentScaleFactors(&scaleX, &scaleY);
            imguiEvent.button.x = (int)(event.button.x * scaleX);
            imguiEvent.button.y = (int)(event.button.y * scaleY);
        }
        ImGui_ImplSDL2_ProcessEvent(&imguiEvent);

        if (waiting_for_key != -1 && event.type == SDL_KEYDOWN) {
            binds[waiting_for_key].keyboardKey = event.key.keysym.sym;
            SaveImGuiSettings();
            waiting_for_key = -1;
            continue;
        }

        if (waiting_for_controller_btn != -1 && event.type == SDL_CONTROLLERBUTTONDOWN) {
            binds[waiting_for_controller_btn].controllerButton = (SDL_GameControllerButton)event.cbutton.button;
            SaveImGuiSettings();
            waiting_for_controller_btn = -1;
            continue;
        }

        SDL_Keycode keyCode = event.key.keysym.sym;
        Uint16 keyMod = event.key.keysym.mod;

        if (event.type == SDL_KEYDOWN && keyCode == SDLK_ESCAPE) {
            showMenu = !showMenu;
            continue;
        }

        if (ImGui::GetIO().WantCaptureKeyboard && (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)) {
            continue;
        }

        if (ImGui::GetIO().WantCaptureMouse && (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL)) {
            continue;
        }

        switch (event.type) {
            case SDL_QUIT:
                isRunning = false;
                break;
            case SDL_KEYUP: {
                SDL_Keycode releasedKey = event.key.keysym.sym;
                for (int i = 0; i < 10; i++) {
                    if (releasedKey == binds[i].keyboardKey) {
                        keys &= ~binds[i].gbaMask;
                    }
                }
                if (releasedKey == SDLK_SPACE) {
                    if (speedUp) {
                        speedUp = false;
                        timeScale = 1.0;
                        SDL_ClearQueuedAudio(1);
                        SDL_PauseAudio(0);
                    }
                }
                break;
            }
            case SDL_KEYDOWN:
                if (keyCode == SDLK_RETURN && (keyMod & KMOD_ALT)) {
                    fullScreenFlags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
                    if (fullScreenFlags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                        SDL_GetWindowDisplayMode(sdlWindow, &sdlDispMode);
                        preFullscreenVideoScale = videoScale;
                    } else {
                        SDL_SetWindowDisplayMode(sdlWindow, &sdlDispMode);
                        videoScale = preFullscreenVideoScale;
                    }
                    SDL_SetWindowFullscreen(sdlWindow, fullScreenFlags);

                    SDL_SetWindowSize(sdlWindow, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale);
                    videoScaleChanged = false;
                } else {
                    SDL_Keycode pressedKey = event.key.keysym.sym;
                    for (int i = 0; i < 10; i++) {
                        if (pressedKey == binds[i].keyboardKey) {
                            keys |= binds[i].gbaMask;
                        }
                    }
                    if (pressedKey == SDLK_r && (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))) {
                        DoSoftReset();
                    } else if (pressedKey == SDLK_p && (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))) {
                        paused = !paused;
                    } else if (pressedKey == SDLK_SPACE) {
                        if (!speedUp) {
                            speedUp = true;
                            timeScale = cheatSpeedMultiplier;
                            SDL_PauseAudio(1);
                        }
                    } else if (pressedKey == SDLK_F10) {
                        paused = true;
                        stepOneFrame = true;
                    }
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    unsigned int w = event.window.data1;
                    unsigned int h = event.window.data2;

                    videoScale = 0;
                    if (w / DISPLAY_WIDTH > videoScale)
                        videoScale = w / DISPLAY_WIDTH;
                    if (h / DISPLAY_HEIGHT > videoScale)
                        videoScale = h / DISPLAY_HEIGHT;
                    if (videoScale < 1)
                        videoScale = 1;

                    videoScaleChanged = true;
                }
                break;
        }
    }
}

u16 Platform_GetKeyInput(void)
{
#ifdef _WIN32
    SharedKeys gamepadKeys = GetXInputKeys();

    speedUp = (gamepadKeys & KEY_SPEEDUP) ? true : false;

    if (speedUp) {
        timeScale = SPEEDUP_SCALE;
        SDL_PauseAudio(1);
    } else {
        timeScale = 1.0f;
        SDL_PauseAudio(0);
    }

    return (gamepadKeys != 0) ? gamepadKeys : keys;
#endif

    return keys | PollControllerButtons();
}

#if ENABLE_VRAM_VIEW
void VramDraw(SDL_Texture *texture)
{
    memset(vramBuffer, 0, sizeof(vramBuffer));
    gpsp_draw_vram_view(vramBuffer);
    SDL_UpdateTexture(texture, NULL, vramBuffer, VRAM_VIEW_WIDTH * sizeof(Uint16));
}
#endif

void VDraw(SDL_Texture *texture)
{
    gpsp_draw_frame(gameImage);
    SDL_UpdateTexture(texture, NULL, gameImage, DISPLAY_WIDTH * sizeof(Uint16));
    REG_VCOUNT = DISPLAY_HEIGHT + 1; // prep for being in VBlank period
}
