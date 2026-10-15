CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++20
CFLAGS ?= -std=c11
LDLIBS += -pthread
CPP_EXAMPLES := ayy regex request test timewheel

.PHONY: all clean
all: $(CPP_EXAMPLES) eventfd

$(CPP_EXAMPLES): %: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

eventfd: eventfd.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(CPP_EXAMPLES) eventfd
