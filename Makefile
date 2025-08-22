CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I$(DRAW_DIR) `pkg-config --cflags cairo pangocairo`
LDFLAGS = `pkg-config --libs cairo pangocairo` -lX11 -lXext -lXcomposite -lXfixes -lfontconfig

TARGET = overlay_cairo
DRAW_DIR = draw
SRCS = main.cpp $(DRAW_DIR)/cairo/draw_cairo.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

deps:
	sudo apt-get install libcairo2-dev libpango1.0-dev libx11-dev libxcomposite-dev libxfixes-dev libfontconfig1-dev

.PHONY: all clean deps