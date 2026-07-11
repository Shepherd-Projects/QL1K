#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
HMODULE remote_module(DWORD pid, const std::wstring& filename) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
    MODULEENTRY32W entry{sizeof(entry)};
    HMODULE result{};
    if (Module32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szModule, filename.c_str()) == 0) { result = entry.hModule; break; }
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return result;
}

DWORD remote_call(HANDLE process, const void* address, void* argument = nullptr) {
    const HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(const_cast<void*>(address)), argument, 0, nullptr);
    if (!thread) throw std::runtime_error("CreateRemoteThread failed");
    WaitForSingleObject(thread, INFINITE);
    DWORD result{};
    if (!GetExitCodeThread(thread, &result)) { CloseHandle(thread); throw std::runtime_error("GetExitCodeThread failed"); }
    CloseHandle(thread);
    return result;
}

std::uintptr_t export_rva(HMODULE image, const char* name) {
    const auto address = reinterpret_cast<std::uintptr_t>(GetProcAddress(image, name));
    if (!address) throw std::runtime_error(std::string("missing export: ") + name);
    return address - reinterpret_cast<std::uintptr_t>(image);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) { std::wcerr << L"usage: ql_fps_injector <pid> <absolute-dll-path>\n"; return 2; }
        const DWORD pid = std::stoul(argv[1]);
        const std::filesystem::path dll = std::filesystem::absolute(argv[2]);
        if (!std::filesystem::is_regular_file(dll)) throw std::runtime_error("DLL not found");
        const HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process) throw std::runtime_error("OpenProcess failed");
        HMODULE remote = remote_module(pid, dll.filename().wstring());
        if (!remote) {
            const std::wstring path = dll.wstring();
            const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
            void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!memory || !WriteProcessMemory(process, memory, path.c_str(), bytes, nullptr))
                throw std::runtime_error("remote path allocation/write failed");
            remote = reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(remote_call(process,
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"), memory)));
            VirtualFreeEx(process, memory, 0, MEM_RELEASE);
            if (!remote) throw std::runtime_error("remote LoadLibraryW failed");
        }
        const HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!local) throw std::runtime_error("local image mapping failed");
        const auto base = reinterpret_cast<std::uintptr_t>(remote);
        const DWORD bootstrap = remote_call(process, reinterpret_cast<void*>(base + export_rva(local, "ql_patch_bootstrap")));
        const DWORD status = remote_call(process, reinterpret_cast<void*>(base + export_rva(local, "ql_patch_status")));
        const DWORD reason_pointer = remote_call(process, reinterpret_cast<void*>(base + export_rva(local, "ql_patch_reason")));
        std::vector<char> reason(256, '\0');
        if (reason_pointer) ReadProcessMemory(process, reinterpret_cast<void*>(reason_pointer), reason.data(), reason.size() - 1, nullptr);
        FreeLibrary(local);
        CloseHandle(process);
        std::cout << "bootstrap=" << bootstrap << " status=" << status << " reason=" << reason.data()
                  << " remote_patch=0x" << std::hex << base << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << " (win32=" << GetLastError() << ")\n";
        return 1;
    }
}
