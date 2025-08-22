# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 `pkg-config --cflags cairo`
LDFLAGS = `pkg-config --libs cairo` -lX11 -lXext -lXcomposite -lXfixes

# Target executable name
TARGET = overlay

# Source files
SRCS = overlay.cpp

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

# Clean up build artifacts
clean:
	rm -f $(TARGET)

# Install dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get install libcairo2-dev libx11-dev libxcomposite-dev libxfixes-dev

# Phony targets
.PHONY: all clean install-deps
