Project 1
Istiak Rahman
14493890

How To Compile & Run
====================
~/src/pssh$ make
  gcc -g -Wall -c builtin.c -o builtin.o
  gcc -g -Wall -c parse.c -o parse.o
  gcc -g -Wall -c pssh.c -o pssh.o
  gcc builtin.o parse.o pssh.o -Wall -lreadline -o pssh
~/src/pssh$ ./pssh

Description
===========
This program replicates the basic actions of the shell by performing actions such as input/output redirection, directory searching, single command executions, as well as multiple pipelined commands. I used a 2D array setup for my pipe file descriptors, essentially initializing an array that allows me to index the specific read/write side of each task in the execution loop. 
