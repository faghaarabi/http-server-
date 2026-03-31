CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LIBFLAGS = -shared -fPIC
SERVER = server
LIBRARY = lib/libhttp.so
DB_READER = db_reader

all: $(SERVER) $(LIBRARY) $(DB_READER)

$(SERVER): src/server.c
	$(CC) $(CFLAGS) -o $(SERVER) src/server.c -ldl

$(LIBRARY): lib/http_handler.c include/http_handler.h
	$(CC) $(CFLAGS) $(LIBFLAGS) lib/http_handler.c -o $(LIBRARY)

$(DB_READER): db_reader.c
	$(CC) $(CFLAGS) -o $(DB_READER) db_reader.c

clean:
	rm -f $(SERVER) $(DB_READER) $(LIBRARY)

run: all
	./$(SERVER)