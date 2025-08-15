CXX := g++
CXXFLAGS := -std=c++11 -Wall -O2
LDFLAGS := -lX11 -lXext -lXcomposite -lXfixes -lXft
INCLUDES := -I/usr/include/freetype2/
SRC := src/overlay.cpp
TARGET := src/overlay

# Pass the font path into the code as a define
FONT := fonts/consolas.ttf
CXXFLAGS += -DFONT_PATH=\"$(FONT)\"

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(INCLUDES)

clean:
	rm -f $(TARGET)
