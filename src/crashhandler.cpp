#include "crashhandler.h"

#if defined(_WIN32)

// fopen / localtime / strftime without the MSVC "use the _s variant" warnings.
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

// MSVC links DbgHelp via pragma; the CMake file also lists it for MinGW.
#if defined(_MSC_VER)
#  pragma comment(lib, "dbghelp.lib")
#endif

namespace
{
    // Directory containing the running executable (no trailing slash).
    std::string exeDir()
    {
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string s(path);
        size_t pos = s.find_last_of("\\/");
        return (pos == std::string::npos) ? std::string(".") : s.substr(0, pos);
    }

    // Walk the faulting thread's stack and write each frame (symbol + source
    // line when a .pdb is available) to the report file.
    void writeStack(std::FILE *f, EXCEPTION_POINTERS *ep)
    {
        HANDLE process = GetCurrentProcess();
        HANDLE thread  = GetCurrentThread();

        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(process, nullptr, TRUE);

        // StackWalk64 mutates the CONTEXT, so hand it a copy.
        CONTEXT ctx = *ep->ContextRecord;

        STACKFRAME64 frame = {};
        DWORD machine;
#if defined(_M_X64)
        machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset    = ctx.Rip;
        frame.AddrPC.Mode      = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrFrame.Mode   = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrStack.Mode   = AddrModeFlat;
#elif defined(_M_IX86)
        machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset    = ctx.Eip;
        frame.AddrPC.Mode      = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrFrame.Mode   = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Esp;
        frame.AddrStack.Mode   = AddrModeFlat;
#else
        std::fprintf(f, "  (unsupported architecture: no stack walk)\n");
        SymCleanup(process);
        return;
#endif

        char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
        SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 511;

        for (int i = 0; i < 64; ++i)
        {
            if (!StackWalk64(machine, process, thread, &frame, &ctx, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (frame.AddrPC.Offset == 0)
                break;

            DWORD64 addr = frame.AddrPC.Offset;

            IMAGEHLP_MODULE64 mod = {};
            mod.SizeOfStruct = sizeof(mod);
            const char *modName = "?";
            if (SymGetModuleInfo64(process, addr, &mod))
                modName = mod.ModuleName;

            DWORD64 disp = 0;
            if (SymFromAddr(process, addr, &disp, sym))
            {
                DWORD            lineDisp = 0;
                IMAGEHLP_LINE64  line     = {};
                line.SizeOfStruct = sizeof(line);
                if (SymGetLineFromAddr64(process, addr, &lineDisp, &line))
                    std::fprintf(f, "  [%02d] %s!%s + 0x%llx   (%s:%lu)\n",
                                 i, modName, sym->Name,
                                 (unsigned long long)disp, line.FileName,
                                 (unsigned long)line.LineNumber);
                else
                    std::fprintf(f, "  [%02d] %s!%s + 0x%llx\n",
                                 i, modName, sym->Name, (unsigned long long)disp);
            }
            else
            {
                std::fprintf(f, "  [%02d] %s + 0x%llx   (no symbols)\n",
                             i, modName, (unsigned long long)addr);
            }
        }

        SymCleanup(process);
    }

    LONG WINAPI crashFilter(EXCEPTION_POINTERS *ep)
    {
        const std::string path = exeDir() + "\\Kemena3D_crash.log";

        if (std::FILE *f = std::fopen(path.c_str(), "w"))
        {
            std::time_t t = std::time(nullptr);
            char ts[64] = {0};
            std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

            const DWORD code = ep->ExceptionRecord->ExceptionCode;

            std::fprintf(f, "Kemena3D - crash report\n");
            std::fprintf(f, "==============================\n");
            std::fprintf(f, "Time:              %s\n", ts);
            std::fprintf(f, "Exception code:    0x%08lx\n", (unsigned long)code);
            std::fprintf(f, "Exception address: %p\n",
                         ep->ExceptionRecord->ExceptionAddress);

            if (code == EXCEPTION_ACCESS_VIOLATION &&
                ep->ExceptionRecord->NumberParameters >= 2)
            {
                const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
                const char *op = (kind == 1) ? "write"
                               : (kind == 8) ? "execute"
                                             : "read";
                std::fprintf(f, "Access violation:  %s at 0x%llx\n", op,
                             (unsigned long long)
                                 ep->ExceptionRecord->ExceptionInformation[1]);
            }

            std::fprintf(f, "\nCall stack (most recent first):\n");
            writeStack(f, ep);
            std::fclose(f);

            const std::string msg =
                "Kemena3D has crashed.\n\n"
                "A crash report was written to:\n" + path +
                "\n\nPlease share that file to help diagnose the problem.";
            MessageBoxA(nullptr, msg.c_str(),
                        "Kemena3D - Crash", MB_OK | MB_ICONERROR);
        }

        return EXCEPTION_EXECUTE_HANDLER; // let the process terminate
    }
} // namespace

namespace kemena
{
    void installCrashHandler()
    {
        SetUnhandledExceptionFilter(crashFilter);
    }
}

#else // !_WIN32

namespace kemena
{
    void installCrashHandler() {}
}

#endif // _WIN32
