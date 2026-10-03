#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SystemSnapshot {
    std::string hostname;
    std::string operatingSystem;
    std::string kernel;
    std::string cpuModel;
    unsigned int cpuCores{};
    double cpuUsage{};
    double memoryUsedGB{};
    double memoryTotalGB{};
    double memoryPercent{};
    double diskUsedGB{};
    double diskTotalGB{};
    double diskPercent{};
    double uptimeSeconds{};
    double loadOne{};
    double loadFive{};
    double loadFifteen{};
    double temperatureC{-1.0};
    std::uint64_t networkRxBytes{};
    std::uint64_t networkTxBytes{};
    int processCount{};
};

struct ProcessInfo {
    int pid{};
    std::string name;
    std::string state;
    double memoryMB{};
};

class SystemMonitor {
public:
    SystemSnapshot capture() const;
    std::vector<ProcessInfo> processes(std::size_t limit = 15) const;
    std::string saveReport(const SystemSnapshot& snapshot) const;

private:
    double sampleCpuUsage() const;
};

namespace ui {
void clearScreen();
void printBanner();
void printDashboard(const SystemSnapshot& snapshot);
void printProcesses(const std::vector<ProcessInfo>& processes);
void printAbout();
void runLiveDashboard(const SystemMonitor& monitor);
int readMenuChoice();
void waitForEnter();
} // namespace ui
