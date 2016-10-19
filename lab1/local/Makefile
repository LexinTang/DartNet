all: client_local server

client_local: client_local.o
	gcc client_local.o -o client_local

client_local.o: client_local.c
	gcc -c -Wall -g client_local.c

server: server.o
	gcc server.o -o server

server.o: server.c
	gcc -c -Wall -g server.c

clean:
	rm -rf *.o
	rm -rf client_local server