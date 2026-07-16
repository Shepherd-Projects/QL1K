#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct RemoteModule {
    HMODULE handle{};
    std::filesystem::path path{};
};

RemoteModule remote_module(DWORD pid, const std::wstring& filename) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    MODULEENTRY32W entry{sizeof(entry)};
    RemoteModule result{};
    if (Module32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szModule, filename.c_str()) == 0) {
            result.handle = entry.hModule;
            result.path = entry.szExePath;
            break;
        }
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

void inject_existing(DWORD pid, const std::filesystem::path& requested_dll) {
    const std::filesystem::path dll = std::filesystem::absolute(requested_dll);
    if (!std::filesystem::is_regular_file(dll)) throw std::runtime_error("DLL not found");
    const HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) throw std::runtime_error("OpenProcess failed");
    const RemoteModule loaded = remote_module(pid, dll.filename().wstring());
    HMODULE remote = loaded.handle;
    if (remote) {
        std::error_code error;
        if (!std::filesystem::equivalent(dll, loaded.path, error) || error) {
            CloseHandle(process);
            throw std::runtime_error("loaded DLL path does not match requested DLL");
        }
    }
    if (!remote) {
        const std::wstring path = dll.wstring();
        const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
        void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!memory || !WriteProcessMemory(process, memory, path.c_str(), bytes, nullptr)) {
            CloseHandle(process);
            throw std::runtime_error("remote path allocation/write failed");
        }
        remote = reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(remote_call(process,
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"), memory)));
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        if (!remote) {
            CloseHandle(process);
            throw std::runtime_error("remote LoadLibraryW failed");
        }
    }
    const HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) {
        CloseHandle(process);
        throw std::runtime_error("local image mapping failed");
    }
    const auto base = reinterpret_cast<std::uintptr_t>(remote);
    const DWORD bootstrap = remote_call(process, reinterpret_cast<void*>(base + export_rva(local, "ql_patch_bootstrap")));
    const DWORD status = remote_call(process, reinterpret_cast<void*>(base + export_rva(local, "ql_patch_status")));
    const DWORD reason_pointer = remote_call(process, reinterpret_cast<void*>(base + export_rva(local, "ql_patch_reason")));
    std::vector<char> reason(256, '\0');
    if (reason_pointer) ReadProcessMemory(process, reinterpret_cast<void*>(reason_pointer), reason.data(), reason.size() - 1, nullptr);
    FreeLibrary(local);
    CloseHandle(process);
    std::cout << "bootstrap=" << bootstrap << " status=" << status << " reason=" << reason.data()
              << " remote_patch=0x" << std::hex << base << std::dec << "\n";
}

std::wstring quote_argument(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring quoted(1, L'\"');
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc >= 4 && _wcsicmp(argv[1], L"--launch") == 0) {
            const std::filesystem::path executable = std::filesystem::absolute(argv[2]);
            if (!std::filesystem::is_regular_file(executable)) {
                throw std::runtime_error("executable not found");
            }
            std::wstring command_line = quote_argument(executable.wstring());
            for (int index = 4; index < argc; ++index) {
                command_line.push_back(L' ');
                command_line += quote_argument(argv[index]);
            }
            std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
            mutable_command.push_back(L'\0');
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            const std::wstring working_directory = executable.parent_path().wstring();
            if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                                CREATE_SUSPENDED, nullptr, working_directory.c_str(), &startup,
                                &process)) {
                throw std::runtime_error("CreateProcessW suspended launch failed");
            }
            try {
                inject_existing(process.dwProcessId, argv[3]);
                if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
                    throw std::runtime_error("ResumeThread failed");
                }
                std::cout << "launched_pid=" << process.dwProcessId << "\n";
            } catch (...) {
                TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                throw;
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 0;
        }
        if (argc != 3) {
            std::wcerr << L"usage: ql_fps_injector <pid> <absolute-dll-path>\n"
                          L"   or: ql_fps_injector --launch <exe> <dll> [game args...]\n";
            return 2;
        }
        inject_existing(std::stoul(argv[1]), argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << " (win32=" << GetLastError() << ")\n";
        return 1;
    }
}
