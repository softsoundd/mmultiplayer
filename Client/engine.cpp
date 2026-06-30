#include <mutex>
#include <thread>
#include <vector>

#include "engine.h"
#include "hook.h"
#include "pattern.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#pragma warning (push)
#pragma warning (disable: 26495)

// D3D9 and window hooks
static struct {
    std::vector<RenderSceneCallback> Callbacks;
    bool ImGuiInitialized = false;
    HRESULT(WINAPI *Original)(IDirect3DDevice9 *) = nullptr;
} renderScene;

static struct {
    HRESULT(WINAPI *Original)
    (IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *) = nullptr;
} resetScene;

static struct {
    bool BlockInput = false;
    byte KeysDown[0x100] = {0};
    std::vector<InputCallback> InputCallbacks;
    std::vector<InputCallback> SuperInputCallbacks;

    HWND Window;
    WNDPROC WndProc = nullptr;
    BOOL(WINAPI *PeekMessage)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
} window;

static HMODULE(WINAPI *LoadLibraryAOriginal)(const char *) = nullptr;
static HMODULE(WINAPI *LoadLibraryWOriginal)(LPCWSTR) = nullptr;
static HMODULE(WINAPI *LoadLibraryExWOriginal)(LPCWSTR, HANDLE, DWORD) = nullptr;

typedef IDirect3D9 *(WINAPI *Direct3DCreate9Fn)(UINT);
typedef HRESULT(WINAPI *D3D9CreateDeviceFn)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND,
                                            DWORD, D3DPRESENT_PARAMETERS *,
                                            IDirect3DDevice9 **);

static Direct3DCreate9Fn Direct3DCreate9Original = nullptr;
static D3D9CreateDeviceFn CreateDeviceOriginal = nullptr;

static std::recursive_mutex d3dHookMutex;
static bool moduleHooksInstalled = false;
static bool direct3DCreate9Hooked = false;
static bool createDeviceHooked = false;
static bool endSceneHooked = false;
static bool resetHooked = false;
static bool renderHooksInstalled = false;
static bool fallbackProbeStarted = false;
static thread_local bool d3dProbeSuppressed = false;

// Engine hooks
static struct {
    std::vector<std::wstring> Queue;
    std::mutex Mutex;
} commands;

static struct {
    std::vector<
        std::pair<Engine::Character, Classes::ASkeletalMeshActorSpawnable *&>>
        Queue;
    std::mutex Mutex;
} spawns;

static struct {
    std::vector<ProcessEventCallback> Callbacks;
    int(__thiscall *Original)(Classes::UObject *, class Classes::UFunction *,
                              void *, void *) = nullptr;
} processEvent;

static struct {
    bool Loading = false;
    void *Base = nullptr;
    std::vector<LevelLoadCallback> PreCallbacks;
    std::vector<LevelLoadCallback> PostCallbacks;
    int(__thiscall *Original)(void *, void *, unsigned long long arg);
} levelLoad;

static struct {
    void *PreBase = nullptr;
    void *PostBase = nullptr;
    std::vector<DeathCallback> PreCallbacks;
    std::vector<DeathCallback> PostCallbacks;
    int (*PreOriginal)();
    int (*PostOriginal)();
} death;

static struct {
    std::vector<ActorTickCallback> Callbacks;
    void *(__thiscall *Original)(Classes::AActor *, void *) = nullptr;
} actorTick;

static struct {
    std::vector<BonesTickCallback> Callbacks;
    void *(__thiscall *Original)(void *, void *) = nullptr;
} bonesTick;

static struct {
    D3DXMATRIX *Matrix;
    int *(__thiscall *Original)(Classes::FMatrix *, void *) = nullptr;
} projectionTick;

static struct {
    std::vector<TickCallback> Callbacks;
    void(__thiscall *Original)(float *, int, float) = nullptr;
} tick;

#pragma warning (pop)

// Forward declaration (required)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// D3D9 and window hook implementations
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HWND ResolveDeviceWindow(IDirect3DDevice9 *device,
                                D3DPRESENT_PARAMETERS *params = nullptr);
static bool InstallD3DFactoryHook(HMODULE module);
static bool InstallCreateDeviceHook(IDirect3D9 *d3d9);
static bool InstallDeviceHooks(IDirect3DDevice9 *device);
static void HandleLoadedModule(HMODULE module);
static void PrepareMenlHooksForLoad();
static void StartFallbackProbe();
static bool IsD3D9ModuleName(const char *module);
static bool IsD3D9ModuleName(LPCWSTR module);
static bool IsMenlModuleName(const char *module);
static bool IsMenlModuleName(LPCWSTR module);
IDirect3D9 *WINAPI Direct3DCreate9Hook(UINT sdkVersion);
HRESULT WINAPI CreateDeviceHook(IDirect3D9 *d3d9, UINT adapter, D3DDEVTYPE deviceType,
                                HWND focusWindow, DWORD behaviorFlags,
                                D3DPRESENT_PARAMETERS *presentationParameters,
                                IDirect3DDevice9 **returnedDevice);

HRESULT WINAPI EndSceneHook(IDirect3DDevice9 *device) {
    if (!renderScene.ImGuiInitialized) {
        const auto gameWindow = ResolveDeviceWindow(device);
        if (!gameWindow) {
            return renderScene.Original(device);
        }

        ImGui::CreateContext();
        if (GetFileAttributesA("C:\\Windows\\Fonts\\verdana.ttf") != INVALID_FILE_ATTRIBUTES) {
            ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 16.0f);
        } else {
            ImGui::GetIO().Fonts->AddFontDefault();
        }

        window.Window = gameWindow;
        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX9_Init(device);

        SetLastError(0);
        window.WndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(gameWindow, GWLP_WNDPROC,
                             reinterpret_cast<LONG_PTR>(WndProcHook)));

        renderScene.ImGuiInitialized = true;
        ImGui::GetIO().MouseDrawCursor = window.BlockInput;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    for (const auto &callback : renderScene.Callbacks) {
        callback(device);
    }

    ImGui::EndFrame();

    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return renderScene.Original(device);
}

HRESULT WINAPI ResetHook(IDirect3DDevice9 *pDevice,
                         D3DPRESENT_PARAMETERS *params) {

    if (renderScene.ImGuiInitialized) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    const auto ret = resetScene.Original(pDevice, params);

    if (SUCCEEDED(ret) && renderScene.ImGuiInitialized) {
        const auto gameWindow = ResolveDeviceWindow(pDevice, params);
        if (gameWindow) {
            window.Window = gameWindow;
        }
        ImGui_ImplDX9_CreateDeviceObjects();
    }

    return ret;
}

void HandleMessage(HWND hWnd, UINT &msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam < sizeof(window.KeysDown)) {
            const auto k = &window.KeysDown[wParam];
            if (!*k) {
                const auto block = window.BlockInput;

                for (const auto &callback : window.SuperInputCallbacks) {
                    callback(msg, wParam);
                }

                if (!block) {
                    for (const auto &callback : window.InputCallbacks) {
                        callback(msg, wParam);
                    }
                }

                *k = 1;
            }
        }

        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < sizeof(window.KeysDown)) {
            const auto k = &window.KeysDown[wParam];
            if (*k) {
                const auto block = window.BlockInput;

                for (const auto &callback : window.SuperInputCallbacks) {
                    callback(msg, wParam);
                }

                if (!block) {
                    for (const auto &callback : window.InputCallbacks) {
                        callback(msg, wParam);
                    }
                }

                *k = 0;
            }
        }

        break;
    }
}

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {

    if (window.BlockInput && renderScene.ImGuiInitialized &&
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        HandleMessage(hWnd, msg, wParam, lParam);
        return true;
    }

    HandleMessage(hWnd, msg, wParam, lParam);
    return window.WndProc ? CallWindowProc(window.WndProc, hWnd, msg, wParam, lParam)
                          : DefWindowProc(hWnd, msg, wParam, lParam);
}

BOOL WINAPI PeekMessageHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                            UINT wMsgFilterMax, UINT wRemoveMsg) {

    const auto ret = window.PeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax,
                                  wRemoveMsg);

    if (lpMsg && (wRemoveMsg & PM_REMOVE)) {
        if (window.BlockInput) {
            if (renderScene.ImGuiInitialized) {
                ImGui_ImplWin32_WndProcHandler(lpMsg->hwnd, lpMsg->message,
                                               lpMsg->wParam, lpMsg->lParam);
            }

            HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam,
                          lpMsg->lParam);

            TranslateMessage(lpMsg);

            if (lpMsg->message != WM_SYSCOMMAND &&
                lpMsg->message != WM_ACTIVATEAPP &&
                lpMsg->message != WM_PAINT) {

                lpMsg->message = WM_NULL;
            }
        } else {
            HandleMessage(lpMsg->hwnd, lpMsg->message, lpMsg->wParam,
                          lpMsg->lParam);
        }
    }

    return ret;
}

// Engine hook implementations
int __fastcall ProcessEventHook(Classes::UObject *object, void *idle,
                                class Classes::UFunction *function, void *args,
                                void *result) {

    auto sum = 0;
    for (auto callback : processEvent.Callbacks) {
        sum += callback(object, function, args, result);
    }

    return sum == 0 ? processEvent.Original(object, function, args, result) : 0;
}

static void ResetSoftimerCheck();

int __fastcall LevelLoadHook(void *this_, void *idle, void **levelInfo,
                             unsigned long long arg) {

    ResetSoftimerCheck();

    const auto levelName = reinterpret_cast<const wchar_t *>(levelInfo[7]);

    for (const auto &callback : levelLoad.PreCallbacks) {
        callback(levelName);
    }

    spawns.Mutex.lock();
    spawns.Queue.clear();
    spawns.Queue.shrink_to_fit();

    levelLoad.Loading = true;
    const auto ret = levelLoad.Original(this_, levelInfo, arg);
    levelLoad.Loading = false;

    spawns.Mutex.unlock();

    for (const auto &callback : levelLoad.PostCallbacks) {
        callback(levelName);
    }

    return ret;
}

int PreDeathHook() {
    for (const auto &callback : death.PreCallbacks) {
        callback();
    }

    return death.PreOriginal();
}

int PostDeathHook() {
    const auto ret = death.PostOriginal();

    for (const auto &callback : death.PostCallbacks) {
        callback();
    }

    return ret;
}

static bool s_needSoftimerCheck = true;

static Classes::UClass *GetSoftTimerPlayerControllerClass() {
    static Classes::UClass *softimerClass = nullptr;

    if (s_needSoftimerCheck && !softimerClass) {
        softimerClass = Classes::UObject::FindClass(
            "Class MirrorsEdgeTweaksScripts.SofTimerPlayerController");
        s_needSoftimerCheck = false;
    }

    return softimerClass;
}

static void ResetSoftimerCheck() {
    s_needSoftimerCheck = true;
}

static bool IsKnownSoftTimerController(Classes::AController *controller) {
    auto softimerClass = GetSoftTimerPlayerControllerClass();
    return softimerClass && controller->IsA(softimerClass);
}

static Classes::UClass *GetTdPlayerControllerClass() {
    static Classes::UClass *tdClass = nullptr;
    if (!tdClass) {
        tdClass = Classes::UObject::FindClass("Class TdGame.TdPlayerController");
    }

    return tdClass;
}

static bool IsTdPlayerController(Classes::AController *controller) {
    if (!controller) {
        return false;
    }

    const auto tdClass = GetTdPlayerControllerClass();
    if (tdClass && controller->IsA(tdClass)) {
        return true;
    }

    return IsKnownSoftTimerController(controller);
}

static BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(hwnd)) {
        return TRUE;
    }

    *reinterpret_cast<HWND *>(lParam) = hwnd;
    return FALSE;
}

static HWND FindGameWindow() {
    HWND hwnd = nullptr;
    EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

static HWND ResolveDeviceWindow(IDirect3DDevice9 *device,
                                D3DPRESENT_PARAMETERS *params) {
    if (params && IsWindow(params->hDeviceWindow)) {
        return params->hDeviceWindow;
    }

    D3DDEVICE_CREATION_PARAMETERS creationParams;
    if (device && SUCCEEDED(device->GetCreationParameters(&creationParams)) &&
        IsWindow(creationParams.hFocusWindow)) {
        return creationParams.hFocusWindow;
    }

    const auto foreground = GetForegroundWindow();
    DWORD processId = 0;
    if (foreground) {
        GetWindowThreadProcessId(foreground, &processId);
        if (processId == GetCurrentProcessId()) {
            return foreground;
        }
    }

    return FindGameWindow();
}

static const char *GetBaseName(const char *module) {
    if (!module) {
        return nullptr;
    }

    const char *slash = strrchr(module, '\\');
    const char *forwardSlash = strrchr(module, '/');
    const char *base = slash;
    if (!base || (forwardSlash && forwardSlash > base)) {
        base = forwardSlash;
    }
    return base ? base + 1 : module;
}

static const wchar_t *GetBaseName(LPCWSTR module) {
    if (!module) {
        return nullptr;
    }

    const wchar_t *slash = wcsrchr(module, L'\\');
    const wchar_t *forwardSlash = wcsrchr(module, L'/');
    const wchar_t *base = slash;
    if (!base || (forwardSlash && forwardSlash > base)) {
        base = forwardSlash;
    }
    return base ? base + 1 : module;
}

static bool IsD3D9ModuleName(const char *module) {
    const auto base = GetBaseName(module);
    return base && _stricmp(base, "d3d9.dll") == 0;
}

static bool IsD3D9ModuleName(LPCWSTR module) {
    const auto base = GetBaseName(module);
    return base && _wcsicmp(base, L"d3d9.dll") == 0;
}

static bool IsMenlModuleName(const char *module) {
    const auto base = GetBaseName(module);
    return base && _stricmp(base, "menl_hooks.dll") == 0;
}

static bool IsMenlModuleName(LPCWSTR module) {
    const auto base = GetBaseName(module);
    return base && _wcsicmp(base, L"menl_hooks.dll") == 0;
}

static void PrepareMenlHooksForLoad() {
    if (!levelLoad.Base || !levelLoad.Original || !death.PreBase ||
        !death.PreOriginal || !death.PostBase || !death.PostOriginal) {
        return;
    }

    Hook::UnTrampolineHook(levelLoad.Base, levelLoad.Original);
    Hook::UnTrampolineHook(death.PreBase, death.PreOriginal);
    Hook::UnTrampolineHook(death.PostBase, death.PostOriginal);

    std::thread([]() {
        for (;;) {
            if (death.PostBase && *reinterpret_cast<byte *>(death.PostBase) == 0xE9) {
                Hook::TrampolineHook(LevelLoadHook, levelLoad.Base,
                                     reinterpret_cast<void **>(&levelLoad.Original));

                Hook::TrampolineHook(PreDeathHook, death.PreBase,
                                     reinterpret_cast<void **>(&death.PreOriginal));

                Hook::TrampolineHook(PostDeathHook, death.PostBase,
                                     reinterpret_cast<void **>(&death.PostOriginal));

                return;
            }

            Sleep(1);
        }
    }).detach();
}

static void HandleLoadedModule(HMODULE module) {
    if (!module) {
        return;
    }

    InstallD3DFactoryHook(module);
}

static bool InstallDeviceHooks(IDirect3DDevice9 *device) {
    if (!device) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(d3dHookMutex);
    void **vtable = *reinterpret_cast<void ***>(device);
    bool status = true;

    if (!endSceneHooked) {
        const bool endSceneStatus = Hook::TrampolineHook(
            EndSceneHook, vtable[D3D9_EXPORT_ENDSCENE],
            reinterpret_cast<void **>(&renderScene.Original));
        endSceneHooked = endSceneStatus;
        status = status && endSceneStatus;
    }

    if (!resetHooked) {
        const bool resetStatus = Hook::TrampolineHook(
            ResetHook, vtable[D3D9_EXPORT_RESET],
            reinterpret_cast<void **>(&resetScene.Original));
        resetHooked = resetStatus;
        status = status && resetStatus;
    }

    renderHooksInstalled = endSceneHooked && resetHooked;
    return renderHooksInstalled;
}

static bool InstallCreateDeviceHook(IDirect3D9 *d3d9) {
    if (!d3d9) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(d3dHookMutex);
    if (createDeviceHooked) {
        return true;
    }

    static const auto D3D9_EXPORT_CREATEDEVICE = 16;
    void **vtable = *reinterpret_cast<void ***>(d3d9);
    createDeviceHooked = Hook::TrampolineHook(
        CreateDeviceHook, vtable[D3D9_EXPORT_CREATEDEVICE],
        reinterpret_cast<void **>(&CreateDeviceOriginal));

    return createDeviceHooked;
}

static bool InstallD3DFactoryHook(HMODULE module) {
    std::lock_guard<std::recursive_mutex> lock(d3dHookMutex);
    if (direct3DCreate9Hooked) {
        StartFallbackProbe();
        return true;
    }

    auto create9 = reinterpret_cast<Direct3DCreate9Fn>(
        GetProcAddress(module, "Direct3DCreate9"));
    if (!create9) {
        return false;
    }

    direct3DCreate9Hooked = Hook::TrampolineHook(
        Direct3DCreate9Hook, reinterpret_cast<void *>(create9),
        reinterpret_cast<void **>(&Direct3DCreate9Original));

    if (direct3DCreate9Hooked) {
        StartFallbackProbe();
    }

    return direct3DCreate9Hooked;
}

struct D3DProbeGuard {
    bool Previous;

    D3DProbeGuard() {
        Previous = d3dProbeSuppressed;
        d3dProbeSuppressed = true;
    }

    ~D3DProbeGuard() {
        d3dProbeSuppressed = Previous;
    }
};

static HRESULT TryCreateProbeDevice(IDirect3D9 *d3d9, HWND window,
                                    IDirect3DDevice9 **device) {
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferWidth = 1;
    d3dpp.BackBufferHeight = 1;
    d3dpp.hDeviceWindow = nullptr;

    struct ProbeAttempt {
        D3DDEVTYPE Type;
        HWND Window;
    } attempts[] = {
        {D3DDEVTYPE_HAL, nullptr},
        {D3DDEVTYPE_HAL, window},
        {D3DDEVTYPE_REF, window},
        {D3DDEVTYPE_REF, nullptr},
    };

    HRESULT hr = D3DERR_NOTAVAILABLE;
    for (const auto &attempt : attempts) {
        d3dpp.hDeviceWindow = attempt.Window;
        hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, attempt.Type,
                                d3dpp.hDeviceWindow,
                                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp,
                                device);
        if (SUCCEEDED(hr) && *device) {
            return hr;
        }
    }

    return hr;
}

static bool TryFallbackProbe() {
    auto module = GetModuleHandleW(L"d3d9.dll");
    if (!module) {
        return false;
    }

    if (!direct3DCreate9Hooked) {
        InstallD3DFactoryHook(module);
    }

    auto create9 = Direct3DCreate9Original;
    if (!create9) {
        create9 = reinterpret_cast<Direct3DCreate9Fn>(
            GetProcAddress(module, "Direct3DCreate9"));
    }

    if (!create9) {
        return false;
    }

    D3DProbeGuard guard;
    IDirect3D9 *d3d9 = create9(D3D_SDK_VERSION);
    if (!d3d9) {
        return false;
    }

    InstallCreateDeviceHook(d3d9);

    const wchar_t className[] = L"MMultiplayerD3DProbeWindow";
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0L, 0L,
                      GetModuleHandle(nullptr), nullptr, nullptr, nullptr,
                      nullptr, className, nullptr};
    const bool registeredByUs = RegisterClassExW(&wc) != 0;
    const bool classAvailable = registeredByUs || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    HWND probeWindow = classAvailable
                           ? CreateWindowW(className, className, WS_OVERLAPPEDWINDOW, 0, 0,
                                           1, 1, nullptr, nullptr, wc.hInstance, nullptr)
                           : nullptr;

    IDirect3DDevice9 *device = nullptr;
    HRESULT hr = TryCreateProbeDevice(d3d9, probeWindow, &device);
    bool hooked = false;
    if (SUCCEEDED(hr) && device) {
        hooked = InstallDeviceHooks(device);
        device->Release();
    }

    if (probeWindow) {
        DestroyWindow(probeWindow);
    }
    if (registeredByUs) {
        UnregisterClassW(className, wc.hInstance);
    }
    d3d9->Release();

    return hooked;
}

static void StartFallbackProbe() {
    std::lock_guard<std::recursive_mutex> lock(d3dHookMutex);
    if (fallbackProbeStarted || renderHooksInstalled) {
        return;
    }

    fallbackProbeStarted = true;
    std::thread([]() {
        Sleep(2000);
        for (auto attempt = 0; attempt < 120; ++attempt) {
            {
                std::lock_guard<std::recursive_mutex> lock(d3dHookMutex);
                if (renderHooksInstalled) {
                    return;
                }
            }

            if (TryFallbackProbe()) {
                return;
            }

            Sleep(1000);
        }
    }).detach();
}

IDirect3D9 *WINAPI Direct3DCreate9Hook(UINT sdkVersion) {
    IDirect3D9 *d3d9 = Direct3DCreate9Original(sdkVersion);
    if (d3d9 && !d3dProbeSuppressed) {
        InstallCreateDeviceHook(d3d9);
    }

    return d3d9;
}

HRESULT WINAPI CreateDeviceHook(IDirect3D9 *d3d9, UINT adapter, D3DDEVTYPE deviceType,
                                HWND focusWindow, DWORD behaviorFlags,
                                D3DPRESENT_PARAMETERS *presentationParameters,
                                IDirect3DDevice9 **returnedDevice) {
    const auto hr = CreateDeviceOriginal(d3d9, adapter, deviceType, focusWindow,
                                         behaviorFlags, presentationParameters,
                                         returnedDevice);
    if (d3dProbeSuppressed) {
        return hr;
    }

    if (SUCCEEDED(hr) && returnedDevice && *returnedDevice) {
        InstallDeviceHooks(*returnedDevice);
    }

    return hr;
}

HMODULE WINAPI LoadLibraryAHook(const char *module) {
    const bool menl = IsMenlModuleName(module);
    if (menl) {
        PrepareMenlHooksForLoad();
    }

    const auto result = LoadLibraryAOriginal(module);
    if (result && IsD3D9ModuleName(module)) {
        HandleLoadedModule(result);
    }

    return result;
}

HMODULE WINAPI LoadLibraryWHook(LPCWSTR module) {
    const bool menl = IsMenlModuleName(module);
    if (menl) {
        PrepareMenlHooksForLoad();
    }

    const auto result = LoadLibraryWOriginal(module);
    if (result && IsD3D9ModuleName(module)) {
        HandleLoadedModule(result);
    }

    return result;
}

HMODULE WINAPI LoadLibraryExWHook(LPCWSTR module, HANDLE file, DWORD flags) {
    const bool menl = IsMenlModuleName(module);
    if (menl) {
        PrepareMenlHooksForLoad();
    }

    const auto result = LoadLibraryExWOriginal(module, file, flags);
    if (result && IsD3D9ModuleName(module)) {
        HandleLoadedModule(result);
    }

    return result;
}

void *__fastcall ActorTickHook(Classes::AActor *actor, void *idle, void *arg) {
    for (const auto &callback : actorTick.Callbacks) {
        callback(actor);
    }

    return actorTick.Original(actor, arg);
}

void *__fastcall BonesTickHook(void *this_, void *idle, void *arg) {
    const auto bones = static_cast<Classes::TArray<Classes::FBoneAtom> *>(
        bonesTick.Original(this_, arg));

    if (bones->Num()) {
        for (const auto &callback : bonesTick.Callbacks) {
            callback(bones);
        }
    }

    return bones;
}

int *__fastcall ProjectionTick(Classes::FMatrix *matrix, void *idle,
                               void *arg) {

    projectionTick.Matrix = reinterpret_cast<D3DXMATRIX *>(matrix);
    return projectionTick.Original(matrix, arg);
}

Classes::ASkeletalMeshActorSpawnable *
SpawnCharacter(Engine::Character character) {

    static const wchar_t *meshes[] = {
        // Faith
        L"CH_TKY_Crim_Fixer.SK_TKY_Crim_Fixer",

        // Kate
        L"CH_TKY_Cop_Patrol_Female.SK_TKY_Cop_Patrol_Female",

        // Celeste
        L"CH_Celeste.SK_Celeste",

        // Assault Celeste
        L"CH_TKY_Cop_Pursuit_Female.SK_TKY_Cop_Pursuit_Female",

        // Jacknife
        L"CH_TKY_Crim_Jacknife.SK_TKY_Crim_Jacknife",

        // Miller
        L"CH_Miller.SK_Miller",

        // Kreeg
        L"CH_Kreeg.SK_Kreeg",

        // Pursuit Cop
        L"CH_TKY_Cop_Pursuit.SK_TKY_Cop_Pursuit",

        // Ghost
        L"TT_Ghost.GhostCharacter_01"
    };

    static const std::vector<std::wstring> materials[] = {
        // Faith
        {
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_69",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_70",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_71",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_72",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_73",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_74",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_75",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_76",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_77",
            L"MaterialInstanceConstant Transient.MaterialInstanceConstant_78",
        },

        // Kate
        {
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Teeth",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Eyes",
            L"Material CH_TKY_Crim_Fixer.unlitAlpha",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Skin",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Hair",
            L"MaterialInstanceConstant CH_TKY_Cop_Patrol_Female.MI_Kate_Cloth",
        },

        // Celeste
        {
            L"Material CH_Celeste.alphablend",
            L"MaterialInstanceConstant CH_Celeste.MI_HairWTF",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Teeth",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Merged_ClothB",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Merged_SkinB",
            L"MaterialInstanceConstant CH_Celeste.MI_Celeste_Eyes",
        },

        // Assault Celeste
        {
            L"MaterialInstanceConstant "
            L"CH_TKY_Cop_Pursuit_Female.MI_CopPursuitFemale",
        },

        // Jacknife
        {
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_Jackknife_Teeth",
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_Jackknife_Cloth",
            L"MaterialInstanceConstant "
            L"CH_TKY_Crim_Jacknife.MI_TKY_Crim_Prowler_Eyes",
            L"Material CH_TKY_Crim_Jacknife.M_Jackknife_Eyeshade",
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_Jackknife_jSkin",
            L"MaterialInstanceConstant CH_TKY_Crim_Jacknife.MI_JackKnife_Hair",
        },

        // Miller
        {
            L"MaterialInstanceConstant CH_Miller.MI_Miller_Eyes",
            L"MaterialInstanceConstant CH_Miller.MI_Teeth",
            L"MaterialInstanceConstant CH_Miller.MI_Miller_Merged_Cloth",
            L"MaterialInstanceConstant CH_Miller.MI_Miller_Merged_Skin",
            L"Material CH_Miller.Unlit",
            L"Material CH_Miller.M_Miller_Brow",
            L"MaterialInstanceConstant CH_Miller.MI_MillerKlurre",
        },

        // Kreeg
        {
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Teeth",
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Cloth",
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Skin",
            L"Material CH_Kreeg.M_Kreeg_Unlit",
            L"MaterialInstanceConstant CH_Kreeg.MI_Kreeg_Eyes",
        },

        // Pursuit Cop
        {
            L"MaterialInstanceConstant CH_TKY_Cop_Pursuit.MI_TKY_Cop_Pursuit",
        },

        // Ghost
        {
            L"Material TT_Ghost.Materials.M_GhostShader_01",
        }
    };

    const auto player = Engine::GetPlayerPawn();
    if (!player) {
        return nullptr;
    }

    const auto actor = static_cast<Classes::ASkeletalMeshActorSpawnable *>(
        player->Spawn(Classes::ASkeletalMeshActorSpawnable::StaticClass(), nullptr, 0,
                      {0}, {0}, nullptr, true));

    actor->SetCollisionType(Classes::ECollisionType::COLLIDE_NoCollision);

    const auto mesh = actor->SkeletalMeshComponent;
    mesh->SetSkeletalMesh(
        static_cast<Classes::USkeletalMesh *>(actor->STATIC_DynamicLoadObject(
            meshes[static_cast<size_t>(character)],
            Classes::USkeletalMesh::StaticClass(), false)),
        false);

    const auto mats = materials[static_cast<size_t>(character)];
    for (auto i = 0UL; i < mats.size(); ++i) {
        mesh->SetMaterial(
            i, static_cast<Classes::UMaterialInterface *>(
                   actor->STATIC_DynamicLoadObject(
                       mats[i].c_str(),
                       Classes::UMaterialInterface::StaticClass(), false)));
    }

    if (character == Engine::Character::Kate ||
        character == Engine::Character::Miller ||
        character == Engine::Character::Kreeg) {

        actor->PrePivot.Z = 94;
    }

    mesh->bUpdateSkelWhenNotRendered = true;
    return actor;
}

void __fastcall TickHook(float *scales, void *idle, int arg, float delta) {
    if (Engine::GetPlayerPawn(true)) {
        // Queues must be executed inside the context of an engine thread in
        // sync with a tick
        if (commands.Queue.size() > 0) {
            auto console = Engine::GetConsole();

            if (console) {
                commands.Mutex.lock();

                for (auto &command : commands.Queue) {
                    console->ConsoleCommand(command.c_str());
                }

                commands.Queue.clear();
                commands.Queue.shrink_to_fit();

                commands.Mutex.unlock();
            }
        }

        if (spawns.Queue.size() > 0) {
            spawns.Mutex.lock();

            for (auto &spawn : spawns.Queue) {
                if (!spawn.second) {
                    spawn.second = SpawnCharacter(spawn.first);
                }
            }

            spawns.Queue.clear();
            spawns.Queue.shrink_to_fit();

            spawns.Mutex.unlock();
        }
    }

    for (auto callback : tick.Callbacks) {
        callback(delta);
    }

    tick.Original(scales, arg, delta);
}

Classes::UTdGameEngine *Engine::GetEngine(bool update) {
    static Classes::UTdGameEngine *cache = nullptr;

    if (!cache || update) {
        const auto &objects = Classes::UObject::GetGlobalObjects();
        for (auto i = 0UL; i < objects.Num(); ++i) {
            const auto object = objects.GetByIndex(i);

            if (!(object &&
                  object->IsA(Classes::UTdGameEngine::StaticClass()))) {

                continue;
            }

            if (object->Outer->GetName() == "Transient") {
                cache = static_cast<Classes::UTdGameEngine *>(object);
                return cache;
            }
        }
    }

    return cache;
}

Classes::UTdGameViewportClient *Engine::GetViewportClient(bool update) {
    static Classes::UTdGameViewportClient *cache = nullptr;

    if (!cache || update) {
        auto engine = GetEngine(update);
        if (engine) {
            cache = static_cast<Classes::UTdGameViewportClient *>(
                engine->GameViewport);
        }
    }

    return cache;
}

Classes::UTdConsole *Engine::GetConsole(bool update) {
    static Classes::UTdConsole *cache = nullptr;

    if (!cache || update) {
        auto viewportClient = GetViewportClient(update);
        if (viewportClient) {
            cache = static_cast<Classes::UTdConsole *>(
                viewportClient->ViewportConsole);
        }
    }

    return cache;
}

void Engine::ExecuteCommand(const wchar_t *command) {
    commands.Mutex.lock();
    commands.Queue.push_back(command);
    commands.Mutex.unlock();
}

Classes::AWorldInfo *Engine::GetWorld(bool update) {
    static Classes::AWorldInfo *cache = nullptr;

    if (levelLoad.Loading) {
        return nullptr;
    }

    if (!cache || update) {
        cache = nullptr;

        const auto& objects = Classes::UObject::GetGlobalObjects();
        for (auto i = 0UL; i < objects.Num(); ++i) {
            const auto object = objects.GetByIndex(i);
            if (!(object && object->IsA(Classes::AWorldInfo::StaticClass()))) {
                continue;
            }

            const auto world = static_cast<Classes::AWorldInfo *>(object);

            for (auto controller = world->ControllerList; controller;
                controller = controller->NextController) {

                if (IsTdPlayerController(controller)) {
                    cache = world;
                    return cache;
                }
            }
        }
    }

    return cache;
}

Classes::ATdPlayerController *Engine::GetPlayerController(bool update) {
    static Classes::ATdPlayerController *cache = nullptr;

    if (levelLoad.Loading) {
        return nullptr;
    }

    if (!cache || update) {
        cache = nullptr;

        auto world = GetWorld(update);
        if (world) {
            for (auto controller = world->ControllerList; controller;
                 controller = controller->NextController) {

                if (IsTdPlayerController(controller)) {
                    if (!static_cast<Classes::ATdPlayerController *>(controller)
                            ->PlayerCamera) {
                        return nullptr;
                    }

                    cache = static_cast<Classes::ATdPlayerController *>(controller);
                    break;
                }
            }
        }
    }

    return cache;
}

Classes::ATdPlayerPawn *Engine::GetPlayerPawn(bool update) {
    static Classes::ATdPlayerPawn *cache = nullptr;

    if (levelLoad.Loading) {
        return nullptr;
    }

    if (!cache || update) {
        cache = nullptr;

        auto controller = GetPlayerController(update);
        if (controller) {
            cache = static_cast<Classes::ATdPlayerPawn *>(
                controller->AcknowledgedPawn);
        }
    }

    return cache;
}

Classes::ATdSPTimeTrialGame* Engine::GetTimeTrialGame(bool update) 
{
    static Classes::ATdSPTimeTrialGame* cache = nullptr;

    if (levelLoad.Loading) 
    {
        return nullptr;
    }

    if (!cache || update) 
    {
        auto world = GetWorld(update);

        if (world) 
        {
            std::string game = world->Game->GetName();

            if (game.find("TdSPTimeTrialGame") == -1) 
            {
                cache = nullptr;
                return cache;
            }

            cache = static_cast<Classes::ATdSPTimeTrialGame*>(world->Game);
            return cache;
        }
    }

    return cache;
}

Classes::ATdSPLevelRace* Engine::GetLevelRace(bool update) 
{
    static Classes::ATdSPLevelRace* cache = nullptr;

    if (levelLoad.Loading) 
    {
        return nullptr;
    }

    if (!cache || update) 
    {
        auto world = GetWorld(update);

        if (world) 
        {
            std::string game = world->Game->GetName();

            if (game.find("TdSPLevelRace") == -1) 
            {
                cache = nullptr;
                return cache;
            }

            cache = static_cast<Classes::ATdSPLevelRace*>(world->Game);
            return cache;
        }
    }

    return cache;
}

void Engine::SpawnCharacter(Character character,
                            Classes::ASkeletalMeshActorSpawnable *&spawned) {
    spawned = nullptr;

    spawns.Mutex.lock();
    spawns.Queue.push_back({character, spawned});
    spawns.Mutex.unlock();
}

void Engine::Despawn(Classes::ASkeletalMeshActorSpawnable *actor) {
    if (!actor) {
        return;
    }

    actor->ShutDown();
}

void Engine::TransformBones(Character character,
                            Classes::TArray<Classes::FBoneAtom> *destBones,
                            Classes::FBoneAtom *src) {

    const auto dest = destBones->Buffer();
    const auto destCount = destBones->Num();

    switch (character) {
    case Character::Faith:
    case Character::Ghost:
        memcpy(dest, src, PLAYER_PAWN_BONE_COUNT * sizeof(Classes::FBoneAtom));
        break;
    case Character::Kate:
        memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
        memcpy(dest + 14, src + 14, 10 * sizeof(Classes::FBoneAtom));
        memcpy(dest + 33, src + 39, sizeof(Classes::FBoneAtom));
        memcpy(dest + 36, src + 42, sizeof(Classes::FBoneAtom));
        memcpy(dest + 39, src + 45, 63 * sizeof(Classes::FBoneAtom));
        break;
    case Character::AssaultCeleste:
        memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
        memcpy(dest + destCount - 63, src + 45, 63 * sizeof(Classes::FBoneAtom));
        memcpy(dest + 17, src + 18, sizeof(Classes::FBoneAtom));
        break;
    case Character::PursuitCop:
        memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
        memcpy(dest + destCount - 63, src + 45, 63 * sizeof(Classes::FBoneAtom));
        memcpy(dest + 15, src + 18, sizeof(Classes::FBoneAtom));
        break;
    case Character::Miller:
    case Character::Celeste:
    case Character::Jacknife:
    case Character::Kreeg:
        memcpy(dest, src, 7 * sizeof(Classes::FBoneAtom));
        memcpy(dest + destCount - 63, src + 45, 63 * sizeof(Classes::FBoneAtom));
        memcpy(dest + 18, src + 18, sizeof(Classes::FBoneAtom));
        break;
    }
}

// Define these to remove the D3DX dependency
D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1,
                                      const D3DXMATRIX *pM2) {

    D3DXMATRIX out;

    for (auto i = 0; i < 4; i++) {
        for (auto j = 0; j < 4; j++) {
            out.m[i][j] =
                pM1->m[i][0] * pM2->m[0][j] + pM1->m[i][1] * pM2->m[1][j] +
                pM1->m[i][2] * pM2->m[2][j] + pM1->m[i][3] * pM2->m[3][j];
        }
    }

    *pOut = out;
    return pOut;
}

D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV,
                                      const D3DXMATRIX *pM) {

    *pOut = {pM->m[0][0] * pV->x + pM->m[1][0] * pV->y + pM->m[2][0] * pV->z +
                 pM->m[3][0] * pV->w,
             pM->m[0][1] * pV->x + pM->m[1][1] * pV->y + pM->m[2][1] * pV->z +
                 pM->m[3][1] * pV->w,
             pM->m[0][2] * pV->x + pM->m[1][2] * pV->y + pM->m[2][2] * pV->z +
                 pM->m[3][2] * pV->w,
             pM->m[0][3] * pV->x + pM->m[1][3] * pV->y + pM->m[2][3] * pV->z +
                 pM->m[3][3] * pV->w};

    return pOut;
}

bool Engine::IsKeyDown(int vk) {
    return !window.BlockInput && vk >= 0 && vk < ARRAYSIZE(window.KeysDown) &&
           window.KeysDown[vk];
}

bool Engine::WorldToScreen(IDirect3DDevice9 *device,
                           Classes::FVector &inOutLocation) {
    const auto controller = Engine::GetPlayerController();
    if (!controller || !projectionTick.Matrix) {
        return false;
    }

    const auto fov = tanf(
        (controller->PlayerCamera->GetFOVAngle() * CONST_Pi / 180.0f) / 2.0f);
    const auto displaySize = ImGui::GetIO().DisplaySize;
    const auto ratioFov = (displaySize.x / displaySize.y) / fov;

    D3DXMATRIX result, proj, world, view;
    proj = *projectionTick.Matrix;

    for (int i = 0; i < 4; ++i) {
        proj.m[i][0] /= fov;
        proj.m[i][1] *= ratioFov;
        proj.m[i][3] = proj.m[i][2];
        proj.m[i][2] *= 0.998f;
    }

    device->GetTransform(D3DTS_VIEW, &view);
    device->GetTransform(D3DTS_WORLD, &world);

    D3DXMatrixMultiply(&result, &proj, &view);
    D3DXMatrixMultiply(&proj, &result, &world);

    D3DXVECTOR4 in(inOutLocation.X, inOutLocation.Y, inOutLocation.Z, 1), out;
    D3DXVec4Transform(&out, &in, &proj);

    inOutLocation = {(((out.x / out.w) + 1.0f) / 2.0f) * displaySize.x,
                     ((1.0f - (out.y / out.w)) / 2.0f) * displaySize.y, out.w};

    return !(out.z < 0 || out.w < 0);
}

HWND Engine::GetWindow() { return window.Window; }

void Engine::OnRenderScene(RenderSceneCallback callback) {
    renderScene.Callbacks.push_back(callback);
}

void Engine::OnProcessEvent(ProcessEventCallback callback) {
    processEvent.Callbacks.push_back(callback);
}

void Engine::OnPreLevelLoad(LevelLoadCallback callback) {
    levelLoad.PreCallbacks.push_back(callback);
}

void Engine::OnPostLevelLoad(LevelLoadCallback callback) {
    levelLoad.PostCallbacks.push_back(callback);
}

void Engine::OnPreDeath(DeathCallback callback) {
    death.PreCallbacks.push_back(callback);
}

void Engine::OnPostDeath(DeathCallback callback) {
    death.PostCallbacks.push_back(callback);
}

void Engine::OnActorTick(ActorTickCallback callback) {
    actorTick.Callbacks.push_back(callback);
}

void Engine::OnBonesTick(BonesTickCallback callback) {
    bonesTick.Callbacks.push_back(callback);
}

void Engine::OnTick(TickCallback callback) {
    tick.Callbacks.push_back(callback);
}

void Engine::OnInput(InputCallback callback) {
    window.InputCallbacks.push_back(callback);
}

void Engine::OnSuperInput(InputCallback callback) {
    window.SuperInputCallbacks.push_back(callback);
}

void Engine::BlockInput(bool block) {
    window.BlockInput = block;
    if (renderScene.ImGuiInitialized && ImGui::GetCurrentContext()) {
        ImGui::GetIO().MouseDrawCursor = block;
    }
}

bool Engine::InitializeD3D() {
    std::lock_guard<std::recursive_mutex> lock(d3dHookMutex);
    if (!moduleHooksInstalled) {
        moduleHooksInstalled =
            Hook::TrampolineHook(LoadLibraryAHook, LoadLibraryA,
                                 reinterpret_cast<void **>(&LoadLibraryAOriginal)) &&
            Hook::TrampolineHook(LoadLibraryWHook, LoadLibraryW,
                                 reinterpret_cast<void **>(&LoadLibraryWOriginal)) &&
            Hook::TrampolineHook(LoadLibraryExWHook, LoadLibraryExW,
                                 reinterpret_cast<void **>(&LoadLibraryExWOriginal));

        if (!moduleHooksInstalled) {
            return false;
        }
    }

    const auto d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (d3d9) {
        InstallD3DFactoryHook(d3d9);
    }

    return true;
}

bool Engine::Initialize() {
    void *ptr = nullptr;

    // GNames
    if (!(ptr = Pattern::FindPattern("\x8B\x0D\x00\x00\x00\x00\x8B\x84\x24\x00"
                                     "\x00\x00\x00\x8B\x04\x81",
                                     "xx????xxx????xxx"))) {

        MessageBoxA(nullptr, "Failed to find GNames", "Failure", MB_ICONERROR);
        return false;
    }

    Classes::FName::GNames = reinterpret_cast<decltype(Classes::FName::GNames)>(
        *reinterpret_cast<void **>(reinterpret_cast<byte *>(ptr) + 2));

    // GObjects
    if (!(ptr = Pattern::FindPattern(
              "\x8B\x15\x00\x00\x00\x00\x8B\x0C\xB2\x8D\x44\x24\x30",
              "xx????xxxxxxx"))) {

        MessageBoxA(nullptr, "Failed to find GObjects", "Failure", MB_ICONERROR);
        return false;
    }

    Classes::UObject::GObjects =
        reinterpret_cast<decltype(Classes::UObject::GObjects)>(
            *reinterpret_cast<void **>(reinterpret_cast<byte *>(ptr) + 2));

    // PeekMessage
    if (!Hook::TrampolineHook(PeekMessageHook, PeekMessageW,
                              reinterpret_cast<void **>(&window.PeekMessage))) {

        MessageBoxA(nullptr, "Failed to hook PeekMessageW", "Failure", MB_ICONERROR);
        return false;
    }

    // ProcessEvent
    if (!(ptr = Pattern::FindPattern(
              "\x56\x8B\xF1\x8B\x0D\x00\x00\x00\x00\x85\xC9\x74\x09",
              "xxxxx????xxxx"))) {

        MessageBoxA(nullptr, "Failed to find ProcessEvent", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(
            ProcessEventHook, ptr,
            reinterpret_cast<void **>(&processEvent.Original))) {

        MessageBoxA(nullptr, "Failed to hook ProcessEvent", "Failure", MB_ICONERROR);
        return false;
    }

    // LevelLoad
    if (!(ptr = levelLoad.Base = Pattern::FindPattern(
              "\x6A\xFF\x68\x00\x00\x00\x00\x64\xA1\x00\x00\x00\x00\x50\x81\xEC"
              "\x00\x00\x00\x00\x53\x55\x56\x57\xA1\x00\x00\x00\x00\x33\xC4\x50"
              "\x8D\x84\x24\x00\x00\x00\x00\x64\xA3\x00\x00\x00\x00\x8B\xE9\x89"
              "\x6C\x24\x00\x00\xFF\x89",
              "???????xxxxxxxxx?xxxxxxxx????xxxxxx?xxxxxxxxxxxxxx??xx"))) {

        MessageBoxA(nullptr, "Failed to find LevelLoad", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(LevelLoadHook, ptr,
                              reinterpret_cast<void **>(&levelLoad.Original))) {

        MessageBoxA(nullptr, "Failed to hook LevelLoad", "Failure", MB_ICONERROR);
        return false;
    }

    // PreDeath
    if (!(ptr = Pattern::FindPattern(
              "\x8D\x4C\x24\x10\xE8\x00\x00\x00\x00\x8B\x4C\x24\x14\x85\xC9\x7C"
              "\x1E\x3B\xCF\x0F\x8D\x00\x00\x00\x00\x8B\x04\x8E\x8B\x40\x08\x25"
              "\x00\x00\x00\x00\x33\xD2\x0B\xC2\x75\xD6\xE9\x00\x00\x00\x00",
              "xxxxx????xxxxxxxxxxxx????xxxxxxx????xxxxxxx????"))) {

        MessageBoxA(nullptr, "Failed to find PreDeath (1)", "Failure", MB_ICONERROR);
        return false;
    }

    if (!(ptr = death.PreBase = Pattern::FindPattern(
              ptr, 0x1000,
              "\xC7\x05\x00\x00\x00\x00\x00\x00\x00\x00\xB8\x00\x00\x00\x00\xC3"
              "\xB8\x00\x00\x00\x00\xC3",
              "xx????????x????xx????x"))) {

        MessageBoxA(nullptr, "Failed to find PreDeath (2)", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(PreDeathHook, ptr,
                              reinterpret_cast<void **>(&death.PreOriginal))) {

        MessageBoxA(nullptr, "Failed to hook PreDeath", "Failure", MB_ICONERROR);
        return false;
    }

    // PostDeath
    if (!(ptr = death.PostBase = Pattern::FindPattern(
              ptr, 0x1000,
              "\x8B\x0D\x00\x00\x00\x00\xC7\x05\x00\x00\x00\x00\x00\x00\x00\x00"
              "\x8B\x01\x8B\x90\x00\x00\x00\x00\xFF\xD2\xB8\x00\x00\x00\x00\xC3"
              "\x8B\xC1\xC7\x00\x00\x00\x00\x00\xC3",
              "??????xx????????xxxx????xxx????xxxxx????x"))) {

        MessageBoxA(nullptr, "Failed to find PostDeath", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(PostDeathHook, ptr,
                              reinterpret_cast<void **>(&death.PostOriginal))) {

        MessageBoxA(nullptr, "Failed to hook PreDeath", "Failure", MB_ICONERROR);
        return false;
    }

    // ActorTick
    if (!(ptr = Pattern::FindPattern(
              "\x55\x8B\xEC\x83\xE4\xF0\x83\xEC\x38\x56\x57\x8B\x81",
              "xxxxxxxxxxxxx"))) {

        MessageBoxA(nullptr, "Failed to find ActorTick", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(ActorTickHook, ptr,
                              reinterpret_cast<void **>(&actorTick.Original))) {

        MessageBoxA(nullptr, "Failed to hook ActorTick", "Failure", MB_ICONERROR);
        return false;
    }

    // BonesTick
    if (!(ptr = Pattern::FindPattern(
              "\xE8\x00\x00\x00\x00\x8B\x74\x24\x14\x8D\x7B\x68",
              "x????xxxxxxx"))) {

        MessageBoxA(nullptr, "Failed to find BonesTick", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(BonesTickHook, RELATIVE_ADDR(ptr, 5),
                              reinterpret_cast<void **>(&bonesTick.Original))) {

        MessageBoxA(nullptr, "Failed to hook BonesTick", "Failure", MB_ICONERROR);
        return false;
    }

    // ProjectionTick
    if (!(ptr = Pattern::FindPattern("\x83\xEC\x3C\xD9\x44\x24\x44",
                                     "xxxxxxx"))) {
        MessageBoxA(nullptr, "Failed to find ProjectionTick", "Failure",
                    MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(
            ProjectionTick, ptr,
            reinterpret_cast<void **>(&projectionTick.Original))) {

        MessageBoxA(nullptr, "Failed to hook ProjectionTick", "Failure",
                    MB_ICONERROR);
        return false;
    }

    // Tick
    if (!(ptr = Pattern::FindPattern(
              "\x83\xEC\x48\x53\x55\x56\x57\x8B\xF9\xE8\x00\x00\x00\x00\x8B\x0D"
              "\x00\x00\x00\x00\x8B\x15\x00\x00\x00\x00\x8B\xE8",
              "xxxxxxxxxx????xx????xx????xx"))) {

        MessageBoxA(nullptr, "Failed to find Tick", "Failure", MB_ICONERROR);
        return false;
    }

    if (!Hook::TrampolineHook(TickHook, ptr,
                              reinterpret_cast<void **>(&tick.Original))) {

        MessageBoxA(nullptr, "Failed to hook Tick", "Failure", MB_ICONERROR);
        return false;
    }

    return true;
}