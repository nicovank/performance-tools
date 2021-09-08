CXX = clang++
CXXFLAGS = -fPIC -Wall -std=c++20 -g -fvisibility=hidden -O3 -Ivendor/libbacktrace
LDFLAGS = -shared -fPIC -Lvendor/libbacktrace/.libs
LDLIBS = -lbacktrace -ldl

BUILD_DIRECTORY = build

all: vendor logger.cpp
	mkdir -p $(BUILD_DIRECTORY)
	$(CXX) $(CXXFLAGS) -c logger.cpp -o $(BUILD_DIRECTORY)/liblogger.o
	$(CXX) $(LDFLAGS) $(BUILD_DIRECTORY)/liblogger.o -o $(BUILD_DIRECTORY)/liblogger.so $(LDLIBS)

.PHONY: format
format:
	clang-format -i logger.cpp Backtrace.hpp

vendor:
	mkdir vendor
	cd vendor && git clone https://github.com/ianlancetaylor/libbacktrace.git
	cd vendor/libbacktrace && ./configure CFLAGS="-fPIC" CC="clang" && make
