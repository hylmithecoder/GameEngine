#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <tchar.h>
#include <sysinfoapi.h>
#include <array>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <utility>
#include <winioctl.h>
#include <cstdint>

using namespace std;

class Environment {
public:
    void detectDriveInfo();
    Environment() {
        detectArchitecture();
        detectCPUName();
        detectGPU();
        detectMemory();
        detectOSVersion();
        detectCoreCount();
        detectDrives();
    }

    void printEnvironment() const {
        cout << "System Architecture: " << (is64Bit ? "64-bit" : "32-bit") << endl;
        cout << "CPU: " << cpuName << endl;
        cout << "GPU(s):" << (gpuNames.empty() ? " None Detected" : "") << endl;
        for (const auto& name : gpuNames) {
            cout << "  - " << name << endl;
        }
        cout << "Logical Cores: " << coreCount << endl;
        cout << "RAM: " << totalRAM_MB << " MB" << endl;
        cout << "OS: " << osVersion << endl;
        cout << "Drives:\n";
        for (const auto& drive : drives) {
            cout << "  - " << drive.letter << ": [" << drive.type << "] (" << drive.mediaType << ")\n";
        }
        // cout  << "Checking "<< drivePath << "...\n";
        // cout << getTBWFromSmartCtl(drivePath) << endl;
    }

    void printDriveInfo(){        
        cout  << "Checking "<< drivePath << "...\n";
        cout << getTBWFromSmartCtl(drivePath) << endl;
    }

private:
    vector<pair<string, string>> getSmartctlDevices();
    string runCommand(const string& command);
    struct DriveInfo {
        string letter;
        string type;
        string mediaType;
    };

    bool is64Bit = false;
    int coreCount = 0;
    unsigned long long totalRAM_MB = 0;
    string cpuName;
    string osVersion;
    vector<string> gpuNames;
    vector<DriveInfo> drives;
    string drivePath = "/dev/sdb"; // Total Bytes Written

    void detectArchitecture() {
        SYSTEM_INFO sysInfo;
        GetNativeSystemInfo(&sysInfo);
        is64Bit = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);
    }

    void detectCPUName() {
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                         TEXT("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                         0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            TCHAR buffer[256];
            DWORD size = sizeof(buffer);
            DWORD type = REG_SZ;
            if (RegQueryValueEx(hKey, TEXT("ProcessorNameString"), NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
                cpuName = buffer;
            }
            RegCloseKey(hKey);
        }
    }

    void detectCoreCount() {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        coreCount = sysInfo.dwNumberOfProcessors;
    }

    void detectMemory() {
        MEMORYSTATUSEX memStat;
        memStat.dwLength = sizeof(memStat);
        if (GlobalMemoryStatusEx(&memStat)) {
            totalRAM_MB = static_cast<unsigned long long>(memStat.ullTotalPhys) / (1024 * 1024);
        }
    }

    void detectOSVersion() {
        OSVERSIONINFOEX osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

        if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
            osVersion = "Windows " + to_string(osvi.dwMajorVersion) + "." + to_string(osvi.dwMinorVersion);
        } else {
            osVersion = "Unknown Windows Version";
        }
    }

    void detectGPU() {
        DISPLAY_DEVICE dd;
        dd.cb = sizeof(dd);
        for (DWORD i = 0; EnumDisplayDevices(NULL, i, &dd, 0); ++i) {
            gpuNames.emplace_back(dd.DeviceString);
        }
    }

    void detectDrives() {
        DWORD drivesBitmask = GetLogicalDrives();
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (!(drivesBitmask & (1 << (letter - 'A')))) continue;

            string rootPath = string(1, letter) + ":\\";
            UINT driveType = GetDriveTypeA(rootPath.c_str());
            string typeStr;
            switch (driveType) {
                case DRIVE_FIXED: typeStr = "Fixed"; break;
                case DRIVE_REMOVABLE: typeStr = "Removable"; break;
                case DRIVE_CDROM: typeStr = "CD-ROM"; break;
                default: typeStr = "Other"; break;
            }

            string mediaType = "Unknown";

            // Coba buka handle ke drive fisik
            string physicalPath = "\\\\.\\" + string(1, letter) + ":";
            HANDLE hDevice = CreateFileA(
                physicalPath.c_str(),
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );

            if (hDevice != INVALID_HANDLE_VALUE) {
                STORAGE_PROPERTY_QUERY query = {};
                query.PropertyId = StorageDeviceSeekPenaltyProperty;
                query.QueryType = PropertyStandardQuery;

                DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty = {};
                DWORD bytesReturned = 0;

                if (DeviceIoControl(
                        hDevice,
                        IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query),
                        &seekPenalty, sizeof(seekPenalty),
                        &bytesReturned, NULL)) {
                    mediaType = seekPenalty.IncursSeekPenalty ? "HDD" : "SSD";
                }

                CloseHandle(hDevice);
            }

            drives.push_back({ string(1, letter), typeStr, mediaType });
        }
    }
    
    string getTBWFromSmartCtl(const string& physicalDrivePath) {
        string command = "smartctl -a " + physicalDrivePath + " 2>&1";
        array<char, 512> buffer;
        string result = "TBW: Unavailable";
    
        FILE* pipe = _popen(command.c_str(), "r");
        if (!pipe) return "TBW: Failed to open smartctl";
    
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            string line = buffer.data();
    
            // Untuk NVMe (umumnya)
            if (line.find("Data Units Written") != string::npos) {
                result = "TBW (Data Units Written): " + line.substr(line.find(":") + 1);
                break;
            }
    
            // Untuk SATA (opsional alternatif)
            if (line.find("Total_LBAs_Written") != string::npos) {
                result = "TBW (LBAs Written): " + line.substr(line.find_last_of(" ") + 1);
                break;
            }
        }
    
        _pclose(pipe);
        return result;
    }
};

inline vector<pair<string, string>> Environment::getSmartctlDevices() {
    vector<pair<string, string>> devices;
    FILE* pipe = popen("smartctl --scan", "r");
    if (!pipe) return devices;

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        string line = buffer;
        size_t devStart = line.find("/dev/");
        if (devStart != string::npos) {
            string devPath = line.substr(devStart, line.find(" ", devStart) - devStart);
            size_t dPos = line.find("-d ");
            string devType = (dPos != string::npos)
                ? line.substr(dPos + 3, line.find(" ", dPos + 3) - (dPos + 3))
                : "";

            devices.emplace_back(devPath, devType);
        }
    }
    pclose(pipe);
    return devices;
}

inline string Environment::runCommand(const string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "ERROR";

    string result;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

inline void Environment::detectDriveInfo() {
    cout << "\n[Drive Info with TBW]" << endl;
    auto devices = getSmartctlDevices();

    for (const auto& [path, type] : devices) {
        string command = "smartctl -a -d " + type + " " + path + " 2>&1";
        cout << "Running command: " << command << endl;
        string output = runCommand(command);

        if (output.find("Unable to detect device type") != string::npos ||
            output.find("Permission denied") != string::npos)
        {
            cout << path << " (" << type << "): Unavailable or inaccessible.\n";
            continue;
        }

        cout << path << " (" << type << "):\n";

        istringstream iss(output);
        string line;
        bool tbwShown = false;

        while (getline(iss, line)) {
            if (line.find("Data Units Written") != string::npos) {
                cout << "  " << line << "\n";
                size_t pos = line.find_last_of(" ");
                if (pos != string::npos) {
                    string valueStr = line.substr(pos + 1);
                    uint64_t value = stoull(valueStr);
                    double tbw = value * 512.0 * 1000.0 / (1024.0 * 1024.0 * 1024.0 * 1024.0);
                    cout << "  Estimated TBW: " << tbw << " TB\n";
                    tbwShown = true;
                }
            }
        
            if (line.find("Total_LBAs_Written") != string::npos ||
                line.find("Host_Writes_32MiB") != string::npos) {
                cout << "  " << line << "\n";
                size_t pos = line.find_last_of(" ");
                if (pos != string::npos) {
                    string valueStr = line.substr(pos + 1);
                    uint64_t value = stoull(valueStr);
                    double tbw = value * 512.0 / (1024.0 * 1024.0 * 1024.0 * 1024.0);
                    cout << "  Estimated TBW: " << tbw << " TB\n";
                    tbwShown = true;
                }
            }
        }        

        if (!tbwShown)
            cout << "  TBW info not found.\n";
    }
}