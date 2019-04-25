# Makefile for Project 5

CC = gcc

all: tictactoeServer tictactoeClient
	
tttServer: tictactoeServer.c
	$(CC) tictactoeServer.c -o tictactoeServer -Wall -std=gnu99
		
tttClient: tictactoeClient.c
	$(CC) tictactoeClient.c -o tictactoeClient -Wall -std=gnu99
	
clean:
	rm tictactoeServer
	rm tictactoeClient