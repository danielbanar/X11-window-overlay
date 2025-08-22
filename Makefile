CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 `pkg-config --cflags cairo pangocairo`
LDFLAGS = `pkg-config --libs cairo pangocairo` -lX11 -lXext -lXcomposite -lXfixes

TARGET = overlay
SRCS = overlay.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install-deps:
	sudo apt-get install libcairo2-dev libpango1.0-dev libx11-dev libxcomposite-dev libxfixes-dev

.PHONY: all clean install-deps
