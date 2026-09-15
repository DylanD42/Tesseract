CXX := g++
CPPFLAGS := -Iraylib/include
CXXFLAGS := -Wall -Wextra

TARGET := program.exe
SOURCES := src/newplace.cpp
OBJECTS := newplace.o
RAYLIB_LIB := raylib/lib/libraylib.a
LDLIBS := -lopengl32 -lgdi32 -lwinmm

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJECTS) $(RAYLIB_LIB)
	$(CXX) -o $@ $(OBJECTS) $(RAYLIB_LIB) $(LDLIBS)

newplace.o: src/newplace.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	.\$(TARGET)

clean:
	powershell -NoProfile -Command "Remove-Item -Force -ErrorAction SilentlyContinue $(OBJECTS), $(TARGET); exit 0"
