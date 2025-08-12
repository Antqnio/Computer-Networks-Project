#!/bin/bash

gcc -Wall -Wextra -Iinclude server.c src/stampa_delimitatore.c src/send_recv_all.c -o server
./server