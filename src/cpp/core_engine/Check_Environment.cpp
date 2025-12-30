#include "../../../include/core_engine/Check_Environment.hpp"
using namespace core_engine;

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
