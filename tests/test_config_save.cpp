// Standalone config regression checks; no game, renderer, or assets are started.
// Windows: tests\run_config_tests.bat (from the repository root).
#include "../src/system/ConfigFile.cpp"
#include <string>

// Only the settings and asset-path services ConfigFile actually uses.
BOOL g_bFullScreen = FALSE, g_bVSync = FALSE;
DWORD g_dwScreenWidth = 640, g_dwScreenHeight = 480;
DWORD g_dwPlayCount = 1, g_dwClearCount = 0;
DWORD g_dwSelectedDisplayAdapterID = 1, g_dwSelectedDisplayModeID = 0;
DWORD g_CachedWaveOutVolume = 0;
int g_dwBitDepth = 32, g_GameMode = 0, g_debugFeaturesEnabled = 0;
bool g_bPlayAsZombie = true, g_bPs1EndingCredits = false, g_bPs1FmvSubtitles = false;
bool g_bSkipUnskippableFmv = false, g_bRunInBackground = false, g_bRunInBackgroundConfig = false;
BYTE g_keyBindingData[32] = {}, g_joystickBindingData[128] = {};
char g_zmNetAddress[64] = "127.0.0.1";
unsigned short g_zmNetPort = 27960;
void SetAssetBase(const char*) {}
void SetSaveRoot(const char*) {}
void SetAssetVersion(const char*) {}
void SetAssetMode(const char*) {}
int GetAssetVersion() { return 0; }
const char* GetAssetRoot() { return "test-assets"; }
const char* GetSaveRoot() { return "test-save"; }
const char* GetAssetModeName() { return ""; }

static void require(bool ok, const char* message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static std::string read(const char* path)
{
    FILE* f = NULL;
    // Windows can briefly deny opening during replacement. That is not a
    // missing file; config saves must preserve it and readers may retry.
    for (int i = 0; i < 100 && f == NULL; ++i) {
        f = fopen(path, "rb");
        if (f == NULL) {
            require(errno == EACCES, "config never disappears during replacement");
            Sleep(1);
        }
    }
    require(f != NULL, "config never disappears during replacement");
    std::string result;
    char data[4096];
    size_t n;
    while ((n = fread(data, 1, sizeof(data), f)) != 0) result.append(data, n);
    require(!ferror(f), "read complete config");
    fclose(f);
    return result;
}

static const char* custom =
    "; keep this comment = unchanged\n[Assets]\nPath=custom-assets\nVersion=JPN\n"
    "[Mods]\nPlayInfested=1\n[Game]\nRunInBackground=1\n[Custom]\nValue=preserve-me\n";

static void preserved(const std::string& data)
{
    require(data.find(custom) == 0, "unowned settings and comments survive");
    require(data.find("KeyDef=") != std::string::npos, "complete input settings");
    require(data.find("Port=27960\n") != std::string::npos, "complete final settings section");
}

int main(int argc, char** argv)
{
    require(argc >= 2, "isolated scratch directory required");
    snprintf(s_path, sizeof(s_path), "%s/config.ini", argv[1]);
    s_pathResolved = TRUE;
    if (argc == 3) {
        for (int i = 0; i < 100; ++i) {
            g_dwScreenWidth = (i & 1) ? 640 : 800;
            ConfigFile_Save();
        }
        return 0;
    }
    require(plat_file_write_atomic(s_path, custom, strlen(custom)) != FALSE, "seed config");
    ConfigFile_Save();
    preserved(read(s_path));

    // A reader denying replacement must never cause the destination's removal.
    std::string before = read(s_path);
    HANDLE locked = CreateFileA(s_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(locked != INVALID_HANDLE_VALUE, "lock original config");
    require(!plat_file_write_atomic(s_path, "broken", 6), "replacement refuses a locked file");
    CloseHandle(locked);
    require(read(s_path) == before, "failed replacement preserves original bytes");

    locked = CreateFileA(s_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    require(locked != INVALID_HANDLE_VALUE, "deny config reads");
    ConfigFile_Save();
    CloseHandle(locked);
    require(read(s_path) == before, "failed input read preserves original bytes");

    std::string large;
    for (int i = 0; i < 600; ++i) large += "; extra line\n";
    require(plat_file_write_atomic(s_path, large.data(), large.size()) != FALSE, "seed oversized config");
    ConfigFile_Save();
    require(read(s_path) == large, "line limit refuses to truncate source");
    require(plat_file_write_atomic(s_path, "", 0) != FALSE, "seed empty config");
    ConfigFile_Save();
    require(read(s_path).empty(), "empty source is not rewritten with owned keys");
    require(remove(s_path) == 0, "remove isolated fixture");
    ConfigFile_Save();
    require(!FileExists(s_path), "missing source is not rewritten with owned keys");

    // Two independent game-like writers and a reader share one configuration.
    require(plat_file_write_atomic(s_path, custom, strlen(custom)) != FALSE, "seed concurrent config");
    ConfigFile_Save();
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    PROCESS_INFORMATION children[2] = {};
    for (int i = 0; i < 2; ++i) {
        char command[1024];
        snprintf(command, sizeof(command), "\"%s\" \"%s\" writer", exe, argv[1]);
        STARTUPINFOA startup = {};
        startup.cb = sizeof(startup);
        require(CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                              NULL, NULL, &startup, &children[i]) != FALSE, "spawn config writer");
    }
    int reads = 0;
    while (WaitForSingleObject(children[0].hProcess, 0) == WAIT_TIMEOUT ||
           WaitForSingleObject(children[1].hProcess, 0) == WAIT_TIMEOUT) {
        preserved(read(s_path));
        ++reads;
        Sleep(1);
    }
    for (int i = 0; i < 2; ++i) {
        DWORD status;
        require(GetExitCodeProcess(children[i].hProcess, &status) && status == 0, "writer succeeded");
        CloseHandle(children[i].hThread);
        CloseHandle(children[i].hProcess);
    }
    preserved(read(s_path));
    printf("PASS: config preservation, failed reads/replacement, oversized/empty/missing files, "
           "and 200 concurrent saves (%d reader checks)\n", reads);
    return 0;
}
