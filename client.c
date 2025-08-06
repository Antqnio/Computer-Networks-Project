#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/constants.h"
#include "include/stampa_delimitatore.h"




int mostraMenuIniziale() {
    int scelta;
    printf("Trivia Quiz\n");
    stampaDelimitatore();
    printf("Menù:\n");
    printf("1 - Comincia una sessione di Trivia\n");
    printf("2 - Esci\n");
    stampaDelimitatore();
    printf("La tua scelta:\n");
    scanf("%d", &scelta);
    while (scelta < 1 || scelta > 2) {
        scanf("%d", &scelta);
        printf("Selezione non valida, riprova:\n");
    }
    return scelta;
}

void mostraMenuNickname(char *nickname, size_t size) {
    char format[50];
    memset(format, 0, sizeof(format)); // Inizializza il formato
    printf("Inserisci il tuo nickname (max %zu caratteri):\n", size - 1);
    memset(nickname, 0, size); // Inizializza il nickname
    /*
     * snprintf(format, sizeof(format), "%%%zus", size - 1);
     *
     * Questo crea dinamicamente una stringa di formato per scanf, come ad esempio "%99s"
     *
     * Vediamo cosa significa "%%%zus":
     * - "%%"     → scrive un singolo '%' nella stringa (in C, %% stampa un %)
     * - "%zu"    → inserisce il valore (size - 1) formattato come unsigned (size_t)
     * - "s"      → parte finale della stringa di formato (scanf vuole qualcosa tipo "%99s")
     *
     * Quindi: "%%%zus" con size = 100 diventa → "%99s"
     */
    snprintf(format, sizeof(format), "%%%zus", size - 1);
    scanf(format, nickname); // Legge il nickname
    if (strlen(nickname) >= size) {
        printf("Nickname troppo lungo: accorcia e riprova.\n");
        exit(EXIT_FAILURE);
    }
}

int main (int argc, char *argv[]) {
    int scelta;
    int porta;
    char nickname[NICKNAME_MAX_LENGTH];
    int ret, sd;
    struct sockaddr_in sv_addr; // Struttura per il server

    if (argc != 2) {
        printf("Numero di argomenti non valido: riavviare il cliet con esattamente 2 argomenti\n");
        exit(EXIT_FAILURE);
    }
    porta = htons(atoi(argv[1])); // Converti la porta in formato di rete
    if (porta <= 0 || porta > 65535) {
        printf("Porta non valida: deve essere un numero tra 1 e 65535\n");
        exit(EXIT_FAILURE);
    }
    scelta = mostraMenuIniziale();
    if (scelta == 2) {
        exit(EXIT_SUCCESS);
    }
    if (scelta != 1) {
        // A regola, non si dovrebbe mai arrivare qui.
        exit(EXIT_FAILURE);
    }
    // Per inviare il nickname al server, dobbiamo prima connetterci
    /* Creazione socket */
    sd = socket(AF_INET, SOCK_STREAM, 0);
    /* Creazione indirizzo del server */
    memset(&sv_addr, 0, sizeof(sv_addr)); // Pulizia
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = porta; // Usa la porta passata come argomento
    inet_pton(AF_INET, SERVER_IP, &sv_addr.sin_addr);
    ret = connect(sd, (struct sockaddr*)&sv_addr, sizeof(sv_addr));
    if (ret == -1) {
        perror("Errore di connessione al server");
        exit(EXIT_FAILURE);
    }
    // Ora che siamo connessi, possiamo inviare il nickname
    mostraMenuNickname(nickname, sizeof(nickname));

    
}