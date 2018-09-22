CXX=g++
CXXFLAGS=-std=c++11 -Wall #-DBOOST_ASIO_ENABLE_HANDLER_TRACKING
#CXXFLAGS+=-ggdb3
CXXFLAGS+=-flto -O2
LDFLAGS=-flto -O2
LIBS=-pthread -lboost_system

all: server

server: main.o server.o connection.o http.o tunnel.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o

depend:
	$(CXX) -std=c++11 -E -MM *.cpp > .depend

-include .depend

.PHONY: all clean depend
