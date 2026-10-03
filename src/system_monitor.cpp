#include "system_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <dirent.h>
#include <sys/select.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

namespace {
constexpr const char* RESET = "\033[0m";
constexpr const char* BOLD = "\033[1m";
constexpr const char* DIM = "\033[2m";
constexpr const char* CYAN = "\033[36m";
constexpr const char* GREEN = "\033[32m";
constexpr const char* YELLOW = "\033[33m";
constexpr const char* RED = "\033[31m";
constexpr const char* MAGENTA = "\033[35m";
constexpr const char* BLUE = "\033[34m";

bool useColor() {
    static const bool enabled = ::isatty(STDOUT_FILENO) && std::getenv("NO_COLOR") == nullptr;
    return enabled;
}

std::string paint(const std::string& text, const char* color) {
    return useColor() ? std::string(color) + text + RESET : text;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n\"");
    const auto last = value.find_last_not_of(" \t\r\n\"");
    if (first == std::string::npos) return "";
    return value.substr(first, last - first + 1);
}

std::string readFirstLine(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return trim(line);
}

std::map<std::string, std::uint64_t> readMemInfo() {
    std::ifstream file("/proc/meminfo");
    std::map<std::string, std::uint64_t> values;
    std::string key;
    std::uint64_t value;
    std::string unit;
    while (file >> key >> value >> unit) {
        if (!key.empty() && key.back() == ':') key.pop_back();
        values[key] = value;
    }
    return values;
}

struct CpuTimes {
    std::uint64_t idle{};
    std::uint64_t total{};
};

CpuTimes readCpuTimes() {
    std::ifstream file("/proc/stat");
    std::string label;
    file >> label;
    if (label != "cpu") throw std::runtime_error("Cannot read /proc/stat");

    std::uint64_t user{}, nice{}, system{}, idle{}, ioWait{};
    std::uint64_t irq{}, softIrq{}, steal{}, guest{}, guestNice{};
    file >> user >> nice >> system >> idle >> ioWait >> irq >> softIrq >> steal >> guest >> guestNice;
    return {idle + ioWait, user + nice + system + idle + ioWait + irq + softIrq + steal};
}

std::string formatBytes(std::uint64_t bytes) {
    constexpr double GB = 1024.0 * 1024.0 * 1024.0;
    constexpr double MB = 1024.0 * 1024.0;
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (bytes >= GB) out << bytes / GB << " GB";
    else out << bytes / MB << " MB";
    return out.str();
}

std::string formatUptime(double seconds) {
    const auto total = static_cast<long long>(seconds);
    const int days = static_cast<int>(total / 86400);
    const int hours = static_cast<int>((total % 86400) / 3600);
    const int minutes = static_cast<int>((total % 3600) / 60);
    std::ostringstream out;
    if (days > 0) out << days << "d ";
    out << hours << "h " << minutes << "m";
    return out.str();
}

std::string bar(double percent, int width = 28) {
    percent = std::clamp(percent, 0.0, 100.0);
    const int filled = static_cast<int>(std::round(percent * width / 100.0));
    const char* color = percent >= 85.0 ? RED : (percent >= 65.0 ? YELLOW : GREEN);
    std::string result = "[";
    result += paint(std::string(filled, '#'), color);
    result += paint(std::string(width - filled, '-'), DIM);
    result += "]";
    return result;
}

std::string statusBadge(double percent) {
    if (percent >= 85.0) return paint("CRITICAL", RED);
    if (percent >= 65.0) return paint("WARNING ", YELLOW);
    return paint("HEALTHY ", GREEN);
}

bool numericName(const char* name) {
    if (*name == '\0') return false;
    for (const char* p = name; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

std::string nowString(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::ostringstream out;
    out << std::put_time(&local, format);
    return out.str();
}

class TerminalMode {
public:
    TerminalMode() : active_(::isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original_) == 0) {
        if (!active_) return;
        termios raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    ~TerminalMode() {
        if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }
    TerminalMode(const TerminalMode&) = delete;
    TerminalMode& operator=(const TerminalMode&) = delete;

private:
    termios original_{};
    bool active_{};
};

bool quitPressed() {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval timeout{0, 0};
    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) <= 0) return false;
    char key{};
    return ::read(STDIN_FILENO, &key, 1) == 1 && (key == 'q' || key == 'Q');
}
} // namespace

double SystemMonitor::sampleCpuUsage() const {
    const CpuTimes first = readCpuTimes();
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    const CpuTimes second = readCpuTimes();
    const auto totalDelta = second.total - first.total;
    const auto idleDelta = second.idle - first.idle;
    return totalDelta == 0 ? 0.0 : 100.0 * (1.0 - static_cast<double>(idleDelta) / totalDelta);
}

SystemSnapshot SystemMonitor::capture() const {
    SystemSnapshot s;

    char host[256]{};
    if (gethostname(host, sizeof(host)) == 0) s.hostname = host;
    else s.hostname = "Unknown";

    std::ifstream osFile("/etc/os-release");
    std::string line;
    while (std::getline(osFile, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            s.operatingSystem = trim(line.substr(12));
            break;
        }
    }
    if (s.operatingSystem.empty()) s.operatingSystem = "Linux";

    utsname systemName{};
    s.kernel = uname(&systemName) == 0 ? std::string(systemName.release) : "Unknown";

    std::ifstream cpuFile("/proc/cpuinfo");
    while (std::getline(cpuFile, line)) {
        if (line.rfind("model name", 0) == 0 && s.cpuModel.empty()) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) s.cpuModel = trim(line.substr(colon + 1));
        }
        if (line.rfind("processor", 0) == 0) ++s.cpuCores;
    }
    if (s.cpuModel.empty()) s.cpuModel = "Unknown processor";
    if (s.cpuCores == 0) s.cpuCores = std::thread::hardware_concurrency();
    s.cpuUsage = sampleCpuUsage();

    const auto memory = readMemInfo();
    const auto totalKB = memory.count("MemTotal") ? memory.at("MemTotal") : 0;
    const auto availableKB = memory.count("MemAvailable") ? memory.at("MemAvailable") : 0;
    const auto usedKB = totalKB > availableKB ? totalKB - availableKB : 0;
    constexpr double KB_PER_GB = 1024.0 * 1024.0;
    s.memoryTotalGB = totalKB / KB_PER_GB;
    s.memoryUsedGB = usedKB / KB_PER_GB;
    s.memoryPercent = totalKB == 0 ? 0.0 : 100.0 * usedKB / totalKB;

    struct statvfs disk{};
    if (statvfs("/", &disk) == 0) {
        const auto total = static_cast<std::uint64_t>(disk.f_blocks) * disk.f_frsize;
        const auto available = static_cast<std::uint64_t>(disk.f_bavail) * disk.f_frsize;
        const auto used = total > available ? total - available : 0;
        constexpr double BYTES_PER_GB = 1024.0 * 1024.0 * 1024.0;
        s.diskTotalGB = total / BYTES_PER_GB;
        s.diskUsedGB = used / BYTES_PER_GB;
        s.diskPercent = total == 0 ? 0.0 : 100.0 * used / total;
    }

    std::ifstream uptimeFile("/proc/uptime");
    uptimeFile >> s.uptimeSeconds;
    std::ifstream loadFile("/proc/loadavg");
    loadFile >> s.loadOne >> s.loadFive >> s.loadFifteen;

    const std::filesystem::path thermal("/sys/class/thermal");
    std::error_code error;
    if (std::filesystem::exists(thermal, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(thermal, error)) {
            if (entry.path().filename().string().rfind("thermal_zone", 0) != 0) continue;
            std::ifstream tempFile(entry.path() / "temp");
            double raw{};
            if (tempFile >> raw) {
                s.temperatureC = raw > 1000.0 ? raw / 1000.0 : raw;
                break;
            }
        }
    }

    std::ifstream netFile("/proc/net/dev");
    while (std::getline(netFile, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string interface = trim(line.substr(0, colon));
        if (interface == "lo") continue;
        std::istringstream data(line.substr(colon + 1));
        std::uint64_t receive{}, transmit{}, ignored{};
        data >> receive;
        for (int i = 0; i < 7; ++i) data >> ignored;
        data >> transmit;
        s.networkRxBytes += receive;
        s.networkTxBytes += transmit;
    }

    DIR* directory = opendir("/proc");
    if (directory) {
        while (dirent* entry = readdir(directory)) {
            if (numericName(entry->d_name)) ++s.processCount;
        }
        closedir(directory);
    }
    return s;
}

std::vector<ProcessInfo> SystemMonitor::processes(std::size_t limit) const {
    std::vector<ProcessInfo> result;
    DIR* directory = opendir("/proc");
    if (!directory) return result;

    const long pageSize = sysconf(_SC_PAGESIZE);
    while (dirent* entry = readdir(directory)) {
        if (!numericName(entry->d_name)) continue;
        ProcessInfo process;
        process.pid = std::atoi(entry->d_name);
        const std::string base = std::string("/proc/") + entry->d_name;
        process.name = readFirstLine(base + "/comm");

        std::ifstream status(base + "/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("State:", 0) == 0) process.state = trim(line.substr(6));
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream value(line.substr(6));
                double kb{};
                value >> kb;
                process.memoryMB = kb / 1024.0;
            }
        }
        if (process.memoryMB == 0.0) {
            std::ifstream statm(base + "/statm");
            long totalPages{}, residentPages{};
            if (statm >> totalPages >> residentPages) {
                process.memoryMB = residentPages * pageSize / (1024.0 * 1024.0);
            }
        }
        if (!process.name.empty()) result.push_back(process);
    }
    closedir(directory);

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.memoryMB > b.memoryMB;
    });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::string SystemMonitor::saveReport(const SystemSnapshot& s) const {
    std::filesystem::create_directories("reports");
    const std::string path = "reports/system_report_" + nowString("%Y%m%d_%H%M%S") + ".txt";
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Could not create report file");

    out << "SYSTEM RESOURCE MONITOR - HEALTH REPORT\n"
        << "Generated: " << nowString("%d %B %Y, %H:%M:%S") << "\n"
        << "============================================================\n"
        << "Hostname         : " << s.hostname << "\n"
        << "Operating System : " << s.operatingSystem << "\n"
        << "Kernel           : " << s.kernel << "\n"
        << "CPU              : " << s.cpuModel << "\n"
        << "Logical cores    : " << s.cpuCores << "\n"
        << std::fixed << std::setprecision(1)
        << "CPU usage        : " << s.cpuUsage << "%\n"
        << "Memory           : " << s.memoryUsedGB << " / " << s.memoryTotalGB
        << " GB (" << s.memoryPercent << "%)\n"
        << "Disk             : " << s.diskUsedGB << " / " << s.diskTotalGB
        << " GB (" << s.diskPercent << "%)\n"
        << "System uptime    : " << formatUptime(s.uptimeSeconds) << "\n"
        << "Running processes: " << s.processCount << "\n"
        << "Network received : " << formatBytes(s.networkRxBytes) << "\n"
        << "Network sent     : " << formatBytes(s.networkTxBytes) << "\n"
        << "============================================================\n";
    const bool healthy = s.cpuUsage < 85.0 && s.memoryPercent < 85.0 && s.diskPercent < 90.0;
    out << "Overall status   : " << (healthy ? "HEALTHY" : "ATTENTION REQUIRED") << "\n";
    return path;
}

void ui::clearScreen() {
    std::cout << "\033[2J\033[H";
}

void ui::printBanner() {
    std::cout << paint("+------------------------------------------------------------+\n", CYAN)
              << paint("|", CYAN) << paint("           LINUX SYSTEM RESOURCE MONITOR                  ", BOLD)
              << paint("|\n", CYAN)
              << paint("|", CYAN) << "        C++17  |  /proc  |  Real-time monitoring           "
              << paint("|\n", CYAN)
              << paint("+------------------------------------------------------------+\n", CYAN);
}

void ui::printDashboard(const SystemSnapshot& s) {
    printBanner();
    std::cout << paint("  SYSTEM\n", MAGENTA)
              << "  Host       : " << s.hostname << "\n"
              << "  OS         : " << s.operatingSystem << "\n"
              << "  Kernel     : " << s.kernel << "\n"
              << "  Uptime     : " << formatUptime(s.uptimeSeconds) << "\n\n"
              << paint("  HARDWARE & UTILIZATION\n", MAGENTA)
              << "  CPU        : " << s.cpuModel << " (" << s.cpuCores << " cores)\n"
              << "  CPU usage  : " << bar(s.cpuUsage) << " " << std::setw(5)
              << std::fixed << std::setprecision(1) << s.cpuUsage << "%  " << statusBadge(s.cpuUsage) << "\n"
              << "  Memory     : " << bar(s.memoryPercent) << " " << std::setw(5)
              << s.memoryPercent << "%  " << statusBadge(s.memoryPercent) << "\n"
              << "               " << s.memoryUsedGB << " GB used / " << s.memoryTotalGB << " GB total\n"
              << "  Disk       : " << bar(s.diskPercent) << " " << std::setw(5)
              << s.diskPercent << "%  " << statusBadge(s.diskPercent) << "\n"
              << "               " << s.diskUsedGB << " GB used / " << s.diskTotalGB << " GB total\n\n"
              << paint("  ACTIVITY\n", MAGENTA)
              << "  Load avg   : " << s.loadOne << "  " << s.loadFive << "  " << s.loadFifteen
              << "  (1m / 5m / 15m)\n"
              << "  Processes  : " << s.processCount << "\n"
              << "  Network    : " << paint("DOWN ", GREEN) << formatBytes(s.networkRxBytes)
              << "   " << paint("UP ", BLUE) << formatBytes(s.networkTxBytes) << "\n"
              << "  Temperature: ";
    if (s.temperatureC >= 0.0) std::cout << s.temperatureC << " C\n";
    else std::cout << "Not exposed by this system/WSL\n";

    const bool attention = s.cpuUsage >= 85.0 || s.memoryPercent >= 85.0 || s.diskPercent >= 90.0;
    std::cout << "\n  Overall    : "
              << (attention ? paint("! ATTENTION REQUIRED", RED) : paint("OK - SYSTEM HEALTHY", GREEN))
              << "\n";
}

void ui::printProcesses(const std::vector<ProcessInfo>& list) {
    printBanner();
    std::cout << paint("  TOP PROCESSES BY MEMORY\n\n", MAGENTA)
              << "  " << std::left << std::setw(9) << "PID" << std::setw(27) << "PROCESS"
              << std::setw(25) << "STATE" << "MEMORY\n"
              << "  " << std::string(72, '-') << "\n";
    for (const auto& p : list) {
        std::string name = p.name.size() > 24 ? p.name.substr(0, 24) : p.name;
        std::string state = p.state.size() > 22 ? p.state.substr(0, 22) : p.state;
        std::cout << "  " << std::left << std::setw(9) << p.pid << std::setw(27) << name
                  << std::setw(25) << state << std::right << std::fixed
                  << std::setprecision(1) << p.memoryMB << " MB\n";
    }
}

void ui::printAbout() {
    printBanner();
    std::cout << paint("  HOW THIS PROJECT WORKS\n\n", MAGENTA)
              << "  Hardware -> Linux Kernel -> /proc files -> C++ program -> User\n\n"
              << "  /proc/stat       CPU utilization\n"
              << "  /proc/meminfo    RAM utilization\n"
              << "  /proc/uptime     System uptime\n"
              << "  /proc/loadavg    System load\n"
              << "  /proc/net/dev    Network totals\n"
              << "  /proc/[PID]      Running process information\n"
              << "  statvfs()        Disk space through a Linux system API\n\n"
              << "  Concepts: OOP, file handling, STL, Linux APIs, processes,\n"
              << "  CPU architecture, memory, storage and error handling.\n";
}

void ui::runLiveDashboard(const SystemMonitor& monitor) {
    // Non-interactive runs render one snapshot instead of waiting for a key.
    if (!::isatty(STDIN_FILENO)) {
        clearScreen();
        printDashboard(monitor.capture());
        std::cout << "\n  Live mode requires an interactive terminal.\n";
        return;
    }

    TerminalMode terminal;
    while (true) {
        clearScreen();
        printDashboard(monitor.capture());
        std::cout << "\n  " << paint("LIVE MODE", GREEN)
                  << " - refreshes automatically | Press Q to return\n" << std::flush;
        for (int i = 0; i < 8; ++i) {
            if (quitPressed()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int ui::readMenuChoice() {
    std::cout << "\n"
              << paint("  MAIN MENU\n", MAGENTA)
              << "  [1] Live dashboard\n"
              << "  [2] Top processes\n"
              << "  [3] Save health report\n"
              << "  [4] How it works\n"
              << "  [0] Exit\n\n"
              << "  Select an option: " << std::flush;
    std::string input;
    std::getline(std::cin, input);
    try {
        return input.empty() ? -1 : std::stoi(input);
    } catch (...) {
        return -1;
    }
}

void ui::waitForEnter() {
    std::cout << "\n  Press Enter to return to the menu..." << std::flush;
    std::string ignored;
    std::getline(std::cin, ignored);
}
