#include <Windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "assets.hpp"
#include "effect.hpp"
#include "th18.hpp"
#include "util.hpp"

CodePatches g_global_patches;
bool g_game_loaded = false;
bool g_game_stage_transition = false;
int g_effect_req = -1;
size_t g_next_effect_timer = 0;

extern int orig_threadproc();
extern "C" int game_threadproc_hook() {
    orig_threadproc();

    printf("Game started\n");

    if (!g_game_stage_transition) {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        Rand::Seed(qpc.LowPart);
        g_next_effect_timer = Rand::RangeFrames(5, 30);
    }
    g_game_loaded = true;
    return 0;
}

extern "C" int __thiscall switch_mode_hook(Main* self) {
    if (self->cur_mode != self->switch_target_mode) {
        if (self->cur_mode == 7) {
            printf("Game ended\n");
            if (self->switch_target_mode != 12) {
                while (Effect::EnabledCount != 0)
                    Effect::Disable(0);
                g_game_stage_transition = false;
            } else {
                g_game_stage_transition = true;
            }
            g_game_loaded = false;
        }
    }
    return self->SwitchMode();
}

int __fastcall post_frame_calc(void*) {
    if (!g_game_loaded || AbilityShop::Instance)
        return 1;

    if (--g_next_effect_timer == 0) {
        static size_t choices[Effect::MAX_EFFECTS];
        size_t choices_count = 0;
        for (size_t i = 0; i < Effect::AllCount; i++) {
            if (!Effect::Infos[i].enabled)
                choices[choices_count++] = i;
        }
        if (choices_count)
            Effect::Enable(choices[Rand::Range(0, choices_count - 1)]);
        g_next_effect_timer = Rand::RangeFrames(5, 30);
    }

    if (g_effect_req != -1) {
        if (Effect::Infos[g_effect_req].enabled) {
            for (int i = 0; i < Effect::EnabledCount; i++) {
                if (Effect::Enabled[i].id == g_effect_req) {
                    Effect::Disable(i);
                    break;
                }
            }
        } else {
            Effect::Enable(g_effect_req);
        }
        g_effect_req = -1;
    }
    Effect::UpdateAll();

    return 1;
}

int __fastcall post_frame_draw(void*) {
    if (!AsciiManager::Instance)
        return 1;

    for (int i = 0; i < Effect::EnabledCount; i++) {
        D3DVECTOR pos = { 4.0f, 470.0f - i * 10.0f, 0.0f };
        //auto fade = min(0xFF, Effect::Enabled[i].frames_active * 8);
        auto fade = Effect::Enabled[i].frames_active > 60 ? 0xFF : (Effect::Enabled[i].frames_active % 8 < 4 ? 0x00 : 0xFF);
        AsciiManager::Instance->color = D3DCOLOR_ARGB(0xFF, 0xFF, fade, fade);
        AsciiManager::Instance->DrawDebugText(&pos, "%s", Effect::Enabled[i].name);
    }
    AsciiManager::Instance->color = 0xFFFFFFFF;

    Effect::DrawAll();

    return 1;
}

DWORD __stdcall console_input_proc(void*) {
    char str[128];
    while (true) {
        if (!fgets(str, sizeof(str), stdin))
            continue;
        g_effect_req = strtol(str, nullptr, 10);
    }

    return 0;
}

EnemyManager* __fastcall enemy_manager_create_hook(const char* filename) {
    if (!EnemyManager::Create(filename))
        return nullptr;

    auto file_manager = EnemyManager::Instance->file_manager;
    if (file_manager->LoadECLFile("chaos.ecl")) {
        MessageBox(NULL, "Failed to load chaos.ecl :(", NULL, MB_ICONERROR);
        DebugBreak();
    }

    Assets::StageAttacks.clear();
    Assets::BossAttacks.clear();
    for (uint32_t i = 0; i < file_manager->sub_count; i++) {
        if (!strncmp(file_manager->subroutines[i].name, "STAGE_", 6))
            Assets::StageAttacks.push_back(file_manager->subroutines[i].name);
        else if (!strncmp(file_manager->subroutines[i].name, "BOSS_", 5))
            Assets::BossAttacks.push_back(file_manager->subroutines[i].name);
    }
    printf("Loaded %zu stage attacks and %zu boss attacks\n", Assets::StageAttacks.size(), Assets::BossAttacks.size());

    return EnemyManager::Instance;
}

int __thiscall enemy_get_global_hook(Enemy* self, int idx) {
    if (idx == -1337)
        return self->random_attack_cur_et;
    return self->GetGlobal(idx);
}

// Runs after the game is mostly initialized (e.g. D3D9 device ready)
extern "C" int entry_hook() {
    // Spawn a console window for debugging
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("UM:CE " __DATE__ "\n");
    printf("Loaded %zu effects\n", Effect::AllCount);
    for (size_t i = 0; i < Effect::AllCount; i++)
        printf("%zu %s\n", i, Effect::Infos[i].name);
    printf("\n");
    SetForegroundWindow(Main::Window);

    // Spawn debug console input thread
    CreateThread(NULL, 0, console_input_proc, NULL, 0, NULL);

    // Load assets
    Assets::Load();

    // Hook enemy manager to import a custom ECL file
    g_global_patches.AddCall(0x442FCF, (void*)enemy_manager_create_hook);
    {
        // Hook enemy ECL global resolution for RandomAttack
        auto patch = enemy_get_global_hook;
        g_global_patches.Add(0x4B6550, &patch, sizeof(patch));
    }
    {
        // Patch ECL thread constructor to memset the whole thing
        uint32_t patch1 = 0x1208; // size
        g_global_patches.Add(0x42CCC4, &patch1, sizeof(patch1));
        uint8_t patch2 = 0x56; // push eax -> push esi
        g_global_patches.Add(0x42CCCD, &patch2, sizeof(patch2));
    }

    // Register post-frame calc function
    auto calc = CalcChain::Create(post_frame_calc);
    calc->flags |= 2;
    CalcChain::RegisterCalc(calc, 1000);

    // Register post-frame draw function
    auto draw = CalcChain::Create(post_frame_draw);
    draw->flags |= 2;
    CalcChain::RegisterDraw(draw, 87);

    return 0;
}

// Runs as early as possible
// This code tries to activate comctl32 6.0
extern "C" void coff2binhack_init() {
    // https://stackoverflow.com/questions/4308503/how-to-enable-visual-styles-without-a-manifest
    WCHAR dir[MAX_PATH];
    ULONG_PTR ulpActivationCookie = FALSE;
    ACTCTXW actCtx =
    {
        sizeof(actCtx),
        ACTCTX_FLAG_RESOURCE_NAME_VALID
            | ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID,
        L"shell32.dll", 0, 0, dir, (LPCWSTR)124
    };
    UINT cch = GetSystemDirectoryW(dir, sizeof(dir) / sizeof(*dir));
    if (cch >= sizeof(dir) / sizeof(*dir)) { return; }
    dir[cch] = TEXT('\0');
    StupidWindowsStuff::ActivationContext = CreateActCtxW(&actCtx);

    ActivateActCtx(StupidWindowsStuff::ActivationContext, &ulpActivationCookie);

    // This class only exists in comctl32 6.0 and above, so querying its window class should load its DLL
    WNDCLASSEX wnd_class = {};
    wnd_class.cbSize = sizeof(wnd_class);
    GetClassInfoEx(NULL, "ListBox", &wnd_class);

    DeactivateActCtx(0, ulpActivationCookie);
}
