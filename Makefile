CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude
TARGET := system-monitor
SOURCES := main.cpp src/system_monitor.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCES) include/system_monitor.hpp
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
