CXX ?= g++
CXXFLAGS ?= -std=c++20
LDLIBS += -pthread
HEADERS := http.hpp ../server.hpp $(wildcard ../../logs/*.h ../../logs/*.hpp)

.PHONY: all clean
all: main

main: main.cc $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f main
