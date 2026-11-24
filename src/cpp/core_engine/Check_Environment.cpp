#include "../../../include/core_engine/Check_Environment.hpp"
using namespace core_engine;

#ifdef _WIN32
// Windows: the same facts from Win32 instead of /proc, uname and lspci.
// GPU names come from Vulkan at renderer start-up, so detectGPU stays empty.
#include "../../../include/core_engine/Platform.hpp"
#include <thread>

void Environment::detectArchitecture() {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: architecture = "64-bit"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: architecture = "64-bit (ARM)"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: architecture = "32-bit"; break;
    default: architecture = "Unknown"; break;
    }
}

void Environment::detectCPUName() {
    char buf[256] = {};
    DWORD size = sizeof buf;
    if (RegGetValueA(HKEY_LOCAL_MACHINE,
                     "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        cpuName = buf;
    if (cpuName.empty()) {
        const char *id = std::getenv("PROCESSOR_IDENTIFIER");
        cpuName = id ? id : "Unknown CPU";
    }
}

void Environment::detectCoreCount() {
    coreCount = (int)std::thread::hardware_concurrency();
}

void Environment::detectMemory() {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms))
        totalRAM_MB = ms.ullTotalPhys / (1024 * 1024);
}

void Environment::detectOSVersion() {
    // GetVersionEx lies without a manifest; RtlGetVersion does not.
    using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);
    osVersion = "Windows";
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW v{};
            v.dwOSVersionInfoSize = sizeof v;
            if (fn(&v) == 0)
                osVersion += (v.dwBuildNumber >= 22000 ? " 11" : " " + to_string(v.dwMajorVersion)) +
                             string(" (build ") + to_string(v.dwBuildNumber) + ")";
        }
}

void Environment::detectGPU() {}

void Environment::detectDrives() {
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i)))
            continue;
        const string root = string(1, char('A' + i)) + ":\\";
        if (GetDriveTypeA(root.c_str()) != DRIVE_FIXED)
            continue;
        ULARGE_INTEGER total{};
        string size = "?";
        if (GetDiskFreeSpaceExA(root.c_str(), nullptr, &total, nullptr))
            size = to_string(total.QuadPart / (1024ull * 1024 * 1024));
        drives.push_back({root, "disk", size});
    }
}

#else

void Environment::detectArchitecture() {
    struct utsname unameData;
    if (uname(&unameData) == 0) {
        architecture = unameData.machine;
        if (architecture == "x86_64") {
            architecture = "64-bit";
        } else if (architecture == "i386" || architecture == "i686") {
            architecture = "32-bit";
        } else {
            architecture += " (Unknown bit-ness)";
        }
    } else {
        architecture = "Unknown";
    }
}

void Environment::detectCPUName() {
    ifstream cpuinfo("/proc/cpuinfo");
    string line;
    while (getline(cpuinfo, line)) {
        if (line.find("model name") != string::npos) {
            size_t pos = line.find(": ");
            if (pos != string::npos) {
                cpuName = line.substr(pos + 2);
                break;
            }
        }
    }
    if (cpuName.empty()) {
        cpuName = "Unknown CPU";
    }
}

void Environment::detectCoreCount() {
    coreCount = sysconf(_SC_NPROCESSORS_ONLN);
}

void Environment::detectMemory() {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        totalRAM_MB = (si.totalram * si.mem_unit) / (1024 * 1024);
    }
}

void Environment::detectOSVersion() {
    // Try reading /etc/os-release first
    ifstream osRelease("/etc/os-release");
    string line;
    string name, version;

    while (getline(osRelease, line)) {
        if (line.find("NAME=") == 0) {
            name = line.substr(5);
            // Remove quotes
            if (name.front() == '"' && name.back() == '"') {
                name = name.substr(1, name.length() - 2);
            }
        } else if (line.find("VERSION=") == 0) {
            version = line.substr(8);
            // Remove quotes
            if (version.front() == '"' && version.back() == '"') {
                version = version.substr(1, version.length() - 2);
            }
        }
    }

    if (!name.empty()) {
        osVersion = name;
        if (!version.empty()) {
            osVersion += " " + version;
        }
    } else {
        // Fallback to uname
        struct utsname unameData;
        if (uname(&unameData) == 0) {
            osVersion = string(unameData.sysname) + " " + string(unameData.release);
        } else {
            osVersion = "Unknown Linux";
        }
    }
}

void Environment::detectGPU() {
    // Try to detect GPU via lspci
    string lspciOutput = runCommand("lspci | grep -i vga");
    if (!lspciOutput.empty()) {
        istringstream iss(lspciOutput);
        string line;
        while (getline(iss, line)) {
            size_t pos = line.find(": ");
            if (pos != string::npos) {
                gpuNames.push_back(line.substr(pos + 2));
            }
        }
    }

    // Also try 3D controller (for discrete GPUs)
    string gpu3dOutput = runCommand("lspci | grep -i '3d controller'");
    if (!gpu3dOutput.empty()) {
        istringstream iss(gpu3dOutput);
        string line;
        while (getline(iss, line)) {
            size_t pos = line.find(": ");
            if (pos != string::npos) {
                gpuNames.push_back(line.substr(pos + 2));
            }
        }
    }
}

void Environment::detectDrives() {
    // Read from /proc/partitions to get block devices
    string lsblkOutput = runCommand("lsblk -d -n -o NAME,TYPE,SIZE | grep disk");
    istringstream iss(lsblkOutput);
    string line;

    while (getline(iss, line)) {
        istringstream lineStream(line);
        string device, type, size;
        lineStream >> device >> type >> size;

        if (!device.empty()) {
            drives.push_back({"/dev/" + device, type, size});
        }
    }
}

#endif // _WIN32
