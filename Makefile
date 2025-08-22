CXX := g++
CXXFLAGS := -std=c++11 -Wall -O2
LDFLAGS := -lX11 -lXext -lXcomposite -lXfixes -lXft -lfontconfig
INCLUDES := -I/usr/include/freetype2/

# Target executable name
TARGET = overlay

# Source files
SRCS = overlay.cpp

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS) $(INCLUDES)

# Clean up build artifacts
clean:
	rm -f $(TARGET)

# Install dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get install libcairo2-dev libx11-dev libxcomposite-dev libxfixes-dev

# Phony targets
.PHONY: all clean install-deps
