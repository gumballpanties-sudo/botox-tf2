// Gate test for the two pieces of dih159x that have to be right BEFORE the game can be trusted
// to report anything: the crash log (src/hooks/crashlog.cpp) and the unique pattern scan
// (src/common.h PatternScanUniqueIn), which is what stands the scoreboard reveal down instead
// of letting it return into whatever else matched its sig.
//
// The crash log half really faults: it raises an access violation, a privileged instruction and
// an integer divide by zero under __except and reads the file back. It writes to its own scratch
// file, never C:\dih\crash.log (CRASHLOG_DIR / CRASHLOG_FILE are redefined for this target).
//
// Build + run:  cmake --build build --config Release --target crashlog_test && build\Release\crashlog_test.exe
#include "../src/globals/common.h"
#include "../src/hooks/crashlog.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;

static void Check(bool ok, const char* what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

// ---- part A: the unique scan -------------------------------------------------------------
// A committed RW page is MEM_COMMIT + PAGE_READWRITE, which is exactly what PatternRegionReadable
// accepts, so the module scanner runs over a buffer unchanged.
static uint8_t* MakeSpan(size_t sz)
{
    auto* p = (uint8_t*)VirtualAlloc(nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p) memset(p, 0xCC, sz);
    return p;
}

static void TestUniqueScan()
{
    puts("unique pattern scan");
    const size_t kSz = 64 * 1024;
    const uint8_t kNeedle[] = { 0x8B, 0xE8, 0xE8, 0x11, 0x22, 0x33, 0x44, 0x3B, 0xC7 };
    const char*   kSig      = "8B E8 E8 ? ? ? ? 3B C7";

    uint8_t* span = MakeSpan(kSz);
    Check(span != nullptr, "scratch span allocated");
    if (!span) return;

    // nothing planted -> no hit, and "not found" is NOT "ambiguous"
    bool amb = true;
    Check(PatternScanUniqueIn(span, kSz, kSig, &amb) == nullptr, "no match -> null");
    Check(amb == false, "no match -> not flagged ambiguous");

    // one occurrence -> that address, not ambiguous
    memcpy(span + 4096, kNeedle, sizeof(kNeedle));
    amb = true;
    uint8_t* hit = PatternScanUniqueIn(span, kSz, kSig, &amb);
    Check(hit == span + 4096, "one match -> its address");
    Check(amb == false, "one match -> not ambiguous");

    // the plain scan still answers the first hit with two planted -- that is the behaviour the
    // unique form exists to refuse, so pin both
    memcpy(span + 40000, kNeedle, sizeof(kNeedle));
    PatternScanToken pat[k_patternMaxTokens];
    const size_t plen = PatternParse(kSig, pat, k_patternMaxTokens);
    Check(PatternScan_SEH(span, kSz, pat, plen) == span + 4096, "plain scan -> first of two");

    amb = false;
    Check(PatternScanUniqueIn(span, kSz, kSig, &amb) == nullptr, "two matches -> null");
    Check(amb == true, "two matches -> flagged ambiguous");

    // Self-overlapping: the rescan resumes at first+1, not first+plen, so a second match that
    // starts one byte inside the first still counts. Resuming past the hit would call a run of
    // four 0xAA unique when the sig matches it twice.
    memset(span + 8192, 0xAA, 4);
    amb = false;
    Check(PatternScanUniqueIn(span, kSz, "AA AA AA", &amb) == nullptr, "overlapping match -> null");
    Check(amb == true, "overlapping match -> ambiguous");

    VirtualFree(span, 0, MEM_RELEASE);
}

// ---- part B: the crash log --------------------------------------------------------------
// Each fault lives in its own noinline function: __try cannot share a frame with anything that
// has a destructor (C2712), and a distinct address per fault is what the dedupe is keyed on.
static volatile int g_sink = 0;

__declspec(noinline) static void FaultRead()
{
    __try { g_sink = *(volatile int*)(uintptr_t)0x18; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void FaultDivide()
{
    __try { volatile int z = 0; g_sink = 7 / z; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void ThrowCpp()
{
    // A C++ throw is a first-chance exception too (0xE06D7363). It must NOT reach the file:
    // the game throws by the thousand and one of those per line is a log nobody can read.
    try { throw 42; } catch (int) {}
}

static std::string ReadAll(const char* path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string out;
    char  buf[4096];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
    CloseHandle(h);
    return out;
}

static int Count(const std::string& hay, const char* needle)
{
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

static void TestCrashLog(const char* path)
{
    puts("crash log");
    DeleteFileA(path);

    CrashLog_Install(GetModuleHandleA(nullptr));
    CrashLog_Mark("test/mark-one");
    CrashLog_Mark("test/mark-two");

    ThrowCpp();      // must leave nothing behind
    FaultRead();
    FaultRead();     // same address -> deduped, one record
    FaultDivide();

    // hitch trace: first call only seeds the clock, the second one measures the gap
    CrashLog_Mark("test/hitch-mark");
    CrashLog_Frame();
    Sleep(200);
    CrashLog_Frame();

    const std::string log = ReadAll(path);
    Check(!log.empty(), "crash.log written");
    Check(log.find("======== botox session") != std::string::npos, "session banner");
    Check(Count(log, "ACCESS_VIOLATION") == 1, "one AV record (second identical fault deduped)");
    Check(log.find("reading 0x18") != std::string::npos, "AV names the address it read");
    Check(log.find("INT_DIVIDE_BY_ZERO") != std::string::npos, "divide by zero recorded");
    Check(log.find("0xE06D7363") == std::string::npos, "C++ throw NOT recorded");
    Check(log.find("test/mark-two") != std::string::npos, "breadcrumbs printed");
    Check(log.find("test/mark-one > test/mark-two") != std::string::npos, "breadcrumbs in order");
    Check(log.find("crashlog_test.exe+0x") != std::string::npos, "frames carry module+RVA");
    Check(log.find("*** botox on stack ***") != std::string::npos, "self-module frame flagged");
    Check(log.find("HITCH") != std::string::npos, "main-thread hitch recorded");
    Check(log.find("test/hitch-mark") != std::string::npos, "hitch line carries breadcrumbs");

    // the stack walk has to produce more than the faulting instruction, or it is not a stack
    const size_t at = log.find("   stack:");
    Check(at != std::string::npos && log.find("     01  ", at) != std::string::npos,
          "stack walk unwound past frame 0");

    if (g_fail) { puts("---- log ----"); fputs(log.c_str(), stdout); }
}

int main()
{
    TestUniqueScan();
    TestCrashLog(CRASHLOG_TEST_FILE);
    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
