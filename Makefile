all: server client

server: server.c
	gcc -o server.exe server.c

client: client.c
	gcc -o client.exe client.c

clean:
	rm -f out *.exe
