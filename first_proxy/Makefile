CXX      ?= clang++
CXXFLAGS ?= -std=c++14 -O2 -g -Wall -Wextra -pthread

first_proxy: main.cpp network.cpp channel.h transponder.h network.h
	$(CXX) $(CXXFLAGS) -o first_proxy main.cpp network.cpp -lpthread

clean:
	rm -f first_proxy

.PHONY: clean
