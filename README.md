# Linux System Resource Monitor

A polished, terminal-based monitoring application written in C++17. It reads
live information exposed by the Linux kernel and presents it as an easy-to-read
dashboard.

## Features

- Live CPU, RAM and disk utilization with health indicators
- Operating system, kernel, hostname and processor details
- System uptime and 1/5/15-minute load averages
- Network receive/transmit totals
- Top processes sorted by memory consumption
- Temperature reading when exposed by the machine
- Timestamped system health reports
- Clean colored terminal interface with automatic refresh

## Architecture

```text
CPU / RAM / Disk / Network
            |
       Linux Kernel
            |
   /proc and Linux APIs
            |
      C++ Application
            |
 Dashboard / Process List / Report
```

The project demonstrates Linux, modern C++, computer architecture, hardware
and software interaction, file handling, object-oriented programming, STL,
processes, system APIs and error handling.

## Run on Windows using WSL

Open PowerShell as Administrator:

```powershell
wsl --install
```

Restart Windows, open **Ubuntu**, then install the tools:

```bash
sudo apt update
sudo apt install build-essential git
```

Open this project inside Ubuntu/WSL, then build and run it:

```bash
make
./system-monitor
```

Alternatively, compile directly:

```bash
g++ -std=c++17 -Wall -Wextra -Iinclude main.cpp src/system_monitor.cpp -o system-monitor
./system-monitor
```

## Menu

1. **Live dashboard** - automatically refreshes; press `Q` to return.
2. **Top processes** - shows processes using the most RAM.
3. **Save health report** - writes a timestamped file inside `reports/`.
4. **How it works** - displays the Linux data sources used by the project.

## Linux data sources

| Source | Purpose |
|---|---|
| `/proc/stat` | CPU utilization |
| `/proc/meminfo` | Physical memory utilization |
| `/proc/uptime` | Time since system boot |
| `/proc/loadavg` | System load averages |
| `/proc/net/dev` | Network byte totals |
| `/proc/[PID]` | Process name, state and memory |
| `statvfs()` | Disk space information |
| `uname()` | Linux kernel information |

## Suggested demonstration

1. Build with `make` to demonstrate Linux compilation.
2. Open the live dashboard and explain each hardware resource.
3. Open another terminal and run a CPU-heavy command to show CPU usage change.
4. Show the process list.
5. Save and open a health report.
6. Explain the flow: hardware -> kernel -> `/proc` -> C++ -> dashboard.

## GitHub upload

Create an empty GitHub repository, then run:

```bash
git init
git add .
git commit -m "Build Linux system resource monitor"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/linux-system-monitor.git
git push -u origin main
```

## Requirements

- Ubuntu, another Linux distribution, or Ubuntu on WSL 2
- A C++17 compiler (`g++`)
- GNU Make

> Temperature may display as unavailable in WSL because Windows does not always
> expose physical temperature sensors to the Linux environment.
