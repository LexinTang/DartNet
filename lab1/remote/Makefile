all: client_remote

client_remote: client_remote.o
	gcc client_remote.o -o client_remote

client_remote.o: client_remote.c
	gcc -c -Wall -g client_remote.c

clean:
	rm -rf *.o
	rm -rf client_remote