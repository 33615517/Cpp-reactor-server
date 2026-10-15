CXX ?= g++
CXXFLAGS ?= -std=c++20
LDLIBS += -pthread
CLIENTS := client1 client2 client3 client4 client5 client6
TOOLS := server tcp_srv tcp_cli
HEADERS := ../source/server.hpp ../source/http/http.hpp $(wildcard ../logs/*.h ../logs/*.hpp)

.PHONY: all clean
all: $(CLIENTS) $(TOOLS)

$(CLIENTS): %: %.cpp $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(TOOLS): %: %.cc $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(CLIENTS) $(TOOLS)
