# How To Compile & Run
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

In version 2 of this project, I implemented more job management functionalities, comprising of a management structure as well as 4 additional built-in functions to handle the flow of jobs inputted into the command line. This project required more critical thinking of how to address signals sent to foreground and background jobs, and how to successfully move to and from the terminal. Overall, this project involved a more in-depth look into proper state tracking for jobs in the terminal and how to correctly send and receive signals within specific foreground or background process groups.
