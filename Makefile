CC=g++
CFLAGS=-std=c++11
LDFLAGS=-std=c++11
SOURCES_SERVER=main.cpp Server.cpp HashTable.cpp

all: server

server: $(SOURCES_SERVER)
	$(CC) $(LDFLAGS) $(SOURCES_SERVER) -o server

clean:
	rm -f *.o server

