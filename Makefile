CXX = g++
DRAW_DIR = draw

# Common flags
CXXFLAGS_COMMON = -std=c++11 -Wall -Wextra -O2
LDFLAGS_COMMON = -lX11 -lXext -lXcomposite -lXfixes -lXrender

# XFT target
XFT_TARGET = overlay_xft
XFT_SRCS = main.cpp $(DRAW_DIR)/draw_x11.cpp
XFT_CFLAGS = $(CXXFLAGS_COMMON) -I$(DRAW_DIR) `pkg-config --cflags xft fontconfig`
XFT_LDFLAGS = `pkg-config --libs xft fontconfig` $(LDFLAGS_COMMON)

# Cairo target
CAIRO_TARGET = overlay_cairo
CAIRO_SRCS = main.cpp $(DRAW_DIR)/draw_cairo.cpp
CAIRO_CFLAGS = $(CXXFLAGS_COMMON) -I$(DRAW_DIR) `pkg-config --cflags cairo pangocairo`
CAIRO_LDFLAGS = `pkg-config --libs cairo pangocairo` $(LDFLAGS_COMMON) -lfontconfig

# Default target: build both
all: $(CAIRO_TARGET) $(XFT_TARGET)

# Build rules
$(XFT_TARGET): $(XFT_SRCS)
	$(CXX) $(XFT_CFLAGS) -o $(XFT_TARGET) $(XFT_SRCS) $(XFT_LDFLAGS)

$(CAIRO_TARGET): $(CAIRO_SRCS)
	$(CXX) $(CAIRO_CFLAGS) -o $(CAIRO_TARGET) $(CAIRO_SRCS) $(CAIRO_LDFLAGS)

# Clean
clean:
	rm -f $(XFT_TARGET) $(CAIRO_TARGET)

# Dependencies installer
deps:
	sudo apt-get install libxft-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxfixes-dev \
	libcairo2-dev libpango1.0-dev

# Phony targets
.PHONY: all clean deps cairo xtf

# Aliases for building individually
cairo: $(CAIRO_TARGET)
xtf: $(XFT_TARGET)
