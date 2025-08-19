#!/bin/bash
# Permette di selezionare la porta passata come argomento al client,
# esegue make e apre il server e un client su due terminali diversi

porta=4242

# Controllo se make e' presente ed, eventualmente, lo installo
if ! command -v make &> /dev/null; then
    echo "Sto installando make..."
    sudo apt update && sudo apt install make -y
fi

# Controllo se gcc e' presente ed, eventualmente, lo installo
if ! command -v gcc &> /dev/null; then
    echo "Sto installando gcc..."
    sudo apt update && sudo apt install gcc -y
fi

# Eseguo make
make

# Controllo se gnome-terminal e' presente ed, eventualmente, lo installo
if ! command -v gnome-terminal &> /dev/null; then
    echo "Sto installando gnome-terminal..."
    sudo apt update && sudo apt install gnome-terminal -y
fi

# Apro il server e un client su terminali separati
gnome-terminal -- bash -c "./server; exec bash"
sleep 1
gnome-terminal -- bash -c "./client $porta; exec bash"