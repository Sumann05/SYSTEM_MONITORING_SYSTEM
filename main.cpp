#include "system_monitor.hpp"

#include <exception>
#include <iostream>

int main() {
    SystemMonitor monitor;

    try {
        while (true) {
            ui::clearScreen();
            ui::printBanner();
            const int choice = ui::readMenuChoice();

            switch (choice) {
                case 1:
                    ui::runLiveDashboard(monitor);
                    break;
                case 2:
                    ui::clearScreen();
                    ui::printProcesses(monitor.processes());
                    ui::waitForEnter();
                    break;
                case 3: {
                    ui::clearScreen();
                    ui::printBanner();
                    std::cout << "\n  Collecting system data...\n";
                    const std::string path = monitor.saveReport(monitor.capture());
                    std::cout << "\n  Report successfully saved to: " << path << "\n";
                    ui::waitForEnter();
                    break;
                }
                case 4:
                    ui::clearScreen();
                    ui::printAbout();
                    ui::waitForEnter();
                    break;
                case 0:
                    ui::clearScreen();
                    std::cout << "Thank you for using Linux System Resource Monitor.\n";
                    return 0;
                default:
                    std::cout << "\n  Invalid option. Please enter 0-4.\n";
                    ui::waitForEnter();
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "\nMonitor error: " << error.what() << '\n';
        std::cerr << "This application must be run inside Linux or Ubuntu WSL.\n";
        return 1;
    }
}
