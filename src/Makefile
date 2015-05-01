CC = gcc

object: client server

client: client.c
	$(CC)  client.c -o client -pthread
server: server.c
	$(CC)  server.c -o server -pthread
clean:
	rm -f client server