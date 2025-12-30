#include <iostream>
#include <vector>
#include <array>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <utility>
#include <cstdint>
#include <fstream>
#include <string>
#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

using namespace std;
namespace fs = std::filesystem;

namespace core_engine {
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
        cout << "System Architecture: " << architecture << endl;
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
            cout << "  - " << drive.device << " [" << drive.type << "] Size: " << drive.size << " GB" << endl;
        }
    }

    void printDriveInfo() {        
        cout << "Checking drives for TBW information...\n";
        detectDriveInfo();
    }
    
private:
    vector<pair<string, string>> getSmartctlDevices();
    string runCommand(const string& command);
    string readFile(const string& filename);
    
    struct DriveInfo {
        string device;
        string type;
        string size;
    };

    string architecture;
    int coreCount = 0;
    unsigned long long totalRAM_MB = 0;
    string cpuName;
    string osVersion;
    vector<string> gpuNames;
    vector<DriveInfo> drives;

    void detectArchitecture();
    void detectCPUName();
    void detectCoreCount();
    void detectMemory();
    void detectOSVersion();
    void detectGPU();
    void detectDrives();    
    string getTBWFromSmartCtl(const string& devicePath, const string& deviceType = "") {
        string command = "smartctl -a";
        if (!deviceType.empty()) {
            command += " -d " + deviceType;
        }
        command += " " + devicePath + " 2>&1";
        
        string output = runCommand(command);
        string result = "TBW: Unavailable";
        
        istringstream iss(output);
        string line;
        
        while (getline(iss, line)) {
            // For NVMe drives
            if (line.find("Data Units Written") != string::npos) {
                cout << "  " << line << "\n";
                // Extract the value and calculate TBW
                size_t pos = line.find_last_of(" ");
                if (pos != string::npos) {
                    try {
                        string valueStr = line.substr(pos + 1);
                        // Remove any non-numeric characters except comma
                        valueStr.erase(remove_if(valueStr.begin(), valueStr.end(), 
                                        [](char c) { return !isdigit(c) && c != ','; }), valueStr.end());
                        valueStr.erase(remove(valueStr.begin(), valueStr.end(), ','), valueStr.end());
                        
                        uint64_t value = stoull(valueStr);
                        double tbw = value * 512.0 * 1000.0 / (1024.0 * 1024.0 * 1024.0 * 1024.0);
                        result = "Estimated TBW: " + to_string(tbw) + " TB";
                    } catch (const exception& e) {
                        result = "TBW: Error parsing value";
                    }
                }
                break;
            }
            
            // For SATA drives
            if (line.find("Total_LBAs_Written") != string::npos) {
                cout << "  " << line << "\n";
                size_t pos = line.find_last_of(" ");
                if (pos != string::npos) {
                    try {
                        string valueStr = line.substr(pos + 1);
                        uint64_t value = stoull(valueStr);
                        double tbw = value * 512.0 / (1024.0 * 1024.0 * 1024.0 * 1024.0);
                        result = "Estimated TBW: " + to_string(tbw) + " TB";
                    } catch (const exception& e) {
                        result = "TBW: Error parsing value";
                    }
                }
                break;
            }
            
            // Alternative for some SATA drives
            if (line.find("Host_Writes_32MiB") != string::npos) {
                cout << "  " << line << "\n";
                size_t pos = line.find_last_of(" ");
                if (pos != string::npos) {
                    try {
                        string valueStr = line.substr(pos + 1);
                        uint64_t value = stoull(valueStr);
                        double tbw = value * 32.0 / 1024.0; // Convert 32MiB units to TB
                        result = "Estimated TBW: " + to_string(tbw) + " TB";
                    } catch (const exception& e) {
                        result = "TBW: Error parsing value";
                    }
                }
                break;
            }
        }
        
        return result;
    }
};

inline vector<pair<string, string>> Environment::getSmartctlDevices() {
    vector<pair<string, string>> devices;
    string output = runCommand("smartctl --scan");
    
    istringstream iss(output);
    string line;
    while (getline(iss, line)) {
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

inline string Environment::readFile(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) return "";
    
    string content;
    string line;
    while (getline(file, line)) {
        content += line + "\n";
    }
    return content;
}

inline void Environment::detectDriveInfo() {
    cout << "\n[Drive Info with TBW]" << endl;
    auto devices = getSmartctlDevices();

    if (devices.empty()) {
        cout << "No devices found via smartctl --scan. Trying common device paths..." << endl;
        // Try common device paths
        vector<string> commonPaths = {"/dev/sda", "/dev/sdb", "/dev/sdc", "/dev/nvme0n1", "/dev/nvme1n1"};
        for (const auto& path : commonPaths) {
            if (fs::exists(path)) {
                devices.emplace_back(path, "");
            }
        }
    }

    for (const auto& [path, type] : devices) {
        cout << "\nChecking device: " << path;
        if (!type.empty()) {
            cout << " (type: " << type << ")";
        }
        cout << endl;
        
        // Check if we have permission to access the device
        if (access(path.c_str(), R_OK) != 0) {
            cout << "  Permission denied. Try running as root (sudo).\n";
            continue;
        }
        
        string tbwInfo = getTBWFromSmartCtl(path, type);
        cout << "  " << tbwInfo << "\n";
    }
};
}