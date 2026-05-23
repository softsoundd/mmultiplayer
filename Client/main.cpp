#include <windows.h>

#include "debug.h"
#include "engine.h"
#include "menu.h"
#include "settings.h"
#include "addon.h"

#include "addons/dolly.h"
#include "addons/client.h"
#include "addons/trainer.h"
#include "addons/misc.h"
#include "addons/chaos/chaos.h"

DWORD WINAPI InitThread(LPVOID param) {
    if (!Engine::InitializeD3D()) {
        MessageBoxA(nullptr, "Failed to initialize D3D hooks", "Fatal", 0);
        return 0;
    }

    Settings::Load();

    Addon *addons[] = { new Client(), new Trainer(), new Dolly(), new Misc(), new Chaos() };

    if (!Engine::Initialize()) {
        MessageBoxA(nullptr, "Failed to initialize engine", "Fatal", 0);
        goto CLEANUP;
    }

    Debug::Initialize();

    if (!Menu::Initialize()) {
        MessageBoxA(nullptr, "Failed to initialize menu", "Fatal", 0);
        goto CLEANUP;
    }
    
    for (auto &addon : addons) {
        if (!addon->Initialize()) {
            MessageBoxA(nullptr, ("Failed to initialize \"" + addon->GetName() + "\"").c_str(), "Fatal", 0);
        }
    }

    return 0;

    CLEANUP:
        for (const auto addon: addons) {
            delete addon;
        }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}