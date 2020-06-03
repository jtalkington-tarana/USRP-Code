all: build/timed_tx

CXXFLAGS=-I/usr/include -I/usr/local/include
LDFLAGS=-luhd -lboost_filesystem -lboost_regex -lboost_thread -lboost_date_time -lboost_system -lboost_program_options -lboost_timer -lusb-1.0 -lpthread

build:
	mkdir build

build/timed_tx: timed_tx.cpp build
	c++ -o $@ timed_tx.cpp ${CXXFLAGS} ${LDFLAGS}

clean:
	rm -rf build
