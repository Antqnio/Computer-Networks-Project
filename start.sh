#!/bin/bash
# Permette di selezionare la porta passata come argomento al client,
# esegue make e apre il server e un client su due terminali diversi

porta=4242

make

gnome-terminal -- bash -c "./server; exec bash"
sleep 1
gnome-terminal -- bash -c "./client $porta; exec bash"