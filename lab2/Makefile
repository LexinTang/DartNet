all: file_browser

file_browser: file_browser.o
	gcc file_browser.o -o file_browser

file_browser.o: file_browser.c
	gcc -c -Wall -g file_browser.c

clean:
	rm -rf *.o
	rm -rf file_browser