#!/bin/bash

porta=4242
gcc -Wall -Wextra -Iinclude client.c src/stampa_delimitatore.c src/send_recv_all.c -o client
./client $porta