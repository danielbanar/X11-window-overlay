CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I$(DRAW_DIR) `pkg-config --cflags xft fontconfig`
LDFLAGS = `pkg-config --libs xft fontconfig` -lX11 -lXext -lXcomposite -lXfixes

TARGET = overlay_xft
DRAW_DIR = draw
SRCS = main.cpp $(DRAW_DIR)/x11/draw_x11.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

deps:
	sudo apt-get install libxft-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxfixes-dev

.PHONY: all clean deps
