porta=4242
gcc -Wall -Wextra -Iinclude client.c src/stampa_delimitatore.c -o client
./client $porta