#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "include/costanti.h"
#include "include/comandi.h"
#include "include/stampa_delimitatore.h"

void termina_con_errore(int sd, const char* msg) {
    // Funzione per terminare il client
    perror(msg);
    close(sd);
    exit(EXIT_FAILURE);
}

char scelta_numerica(int min, int max) {
    // Funzione per leggere una scelta numerica da tastiera
    // Uso un char perché un byte è sufficiente per le scelte del menu
    int scelta;
    while (scanf("%d", &scelta) == -1 || scelta < min || scelta > max) { // Controlla se la scelta è valida
        // Se scanf fallisce (per esempio, se provo a inserire un char da tastiera) o la scelta non è valida, stampa un messaggio di erro
        printf("Scelta non valida, riprova:\n");
        scanf("%d", &scelta);
        // Pulisco il buffer dell'input
        while (getchar() != '\n'); // leggo e scarto tutto fino a fine riga
    }
    return (char)scelta; // Ritorna la scelta valida
}

int mostra_menu_iniziale() {
    int scelta;
    printf("Trivia Quiz\n");
    stampa_delimitatore();
    printf("Menù:\n");
    printf("1 - Comincia una sessione di Trivia\n");
    printf("2 - Esci\n");
    stampa_delimitatore();
    printf("La tua scelta:\n");
    scelta = scelta_numerica(1, 2);
    return scelta;
}

void mostra_menu_nickname(char *nickname, size_t size) {
    char format[50];
    int nickname_valido = 0; // Flag per verificare se il nickname è valido
    while(!nickname_valido) {
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
        //printf("Nickname inserito: %s\n", nickname);
        if (strlen(nickname) == 0) {
            printf("Nickname non valido, riprova:\n");
        }
        nickname_valido = 1;
    }
}

void stampa_menu_temi(const char* temi, int numero_di_temi) {
    printf("Quiz disponibili:\n");
    stampa_delimitatore();
    // Stampa i temi separati da '\n'
    const char* start = temi;
    const char* end = strchr(start, '\n'); // Trova il primo '\n'
    int i = 1;
    while (end != NULL && i <= numero_di_temi) {
        printf("%d - %.*s\n", i, (int)(end - start), start);
        start = end + 1; // Passa al prossimo tema
        end = strchr(start, '\n'); // Trova il prossimo '\n'
        i++;
    }
    stampa_delimitatore();
}


int main (int argc, char *argv[]) {
    char scelta; // Variabile per la scelta del menu iniziale e del tema di gioco
    int porta;
    char nickname[NICKNAME_MAX_LENGTH];
    char temi[1024]; // Buffer per i temi
    int ret, sd;
    size_t dim, byte_ricevuti;
    uint32_t lunghezza_nickname; // Lunghezza del nickname da inviare al server
    uint32_t lunghezza_nickname_net; // Lunghezza nickname in network order
    struct sockaddr_in sv_addr; // Struttura per il server
    uint32_t stato_del_nickname = NICKNAME_ERRATO; // Flag per verificare se il nickname è già registrato
    uint32_t numero_di_temi;
    if (argc != 2) {
        printf("Numero di argomenti non valido: riavviare il cliet con esattamente 2 argomenti\n");
        exit(EXIT_FAILURE);
    }
    porta = htons(atoi(argv[1])); // Converti la porta in formato di rete
    if (porta <= 0 || porta > 65535) {
        printf("Porta non valida: deve essere un numero tra 1 e 65535\n");
        exit(EXIT_FAILURE);
    }
    scelta = mostra_menu_iniziale();
    if (scelta == 2) {
        exit(EXIT_SUCCESS);
    }
    if (scelta != 1) {
        // A regola, non si dovrebbe mai arrivare qui.
        exit(EXIT_FAILURE);
    }
    // Per inviare il nickname al server, dobbiamo prima connetterci. Proviamo a connetterci al server.
    // Se la connessione fallisce, aspettiamo 5 secondi e riproviamo.
    // Finché il client non esegue Ctrl+C, continuerà a provare a connettermi al server.
    ret = -1; // Inizializza ret a -1 per entrare nel while
    while(ret == -1) {
        /* Creazione socket */
        sd = socket(AF_INET, SOCK_STREAM, 0);
        /* Creazione indirizzo del server */
        memset(&sv_addr, 0, sizeof(sv_addr)); // Pulizia
        sv_addr.sin_family = AF_INET;
        sv_addr.sin_port = porta; // Usa la porta passata come argomento
        inet_pton(AF_INET, SERVER_IP, &sv_addr.sin_addr);
        ret = connect(sd, (struct sockaddr*)&sv_addr, sizeof(sv_addr));
        if (ret == -1) {
            perror("Errore di connessione al server. Riprovo in 5 secondi");
            sleep(5); // Attendi 5 secondi prima di riprovare
        }
    }
    // Ora che siamo connessi, possiamo inviare il nickname:
    while (stato_del_nickname == NICKNAME_ERRATO) {
        // Inserimento del nickname (sarà troncato se supera la lunghezza massima, quindi sarà semppre valido)
        mostra_menu_nickname(nickname, sizeof(nickname));
        // Invia la lunghezza del nickname al server
        // printf("Nickname da inviare: %s\n", nickname);
        lunghezza_nickname = strlen(nickname); // Invio al server la quantita di dati
        // printf("Lunghezza nickname: %u\n", lunghezza_nickname);
        lunghezza_nickname_net = htonl(lunghezza_nickname); // Converto in formato network
        // printf("Lunghezza nickname da inviare: %u\n", lunghezza_nickname_net);

        // Invia la lunghezza del nickname (quindi quanti byte il server si aspetta)
        dim = sizeof(lunghezza_nickname_net);
        ret = send(sd, (void*)&lunghezza_nickname_net, dim, 0);
        if (ret == -1) {
            termina_con_errore(sd, "Errore nell'invio della lunghezza nickname al server"); // Termina il client
        }
        // Invia il nickname al server
        dim = lunghezza_nickname;
        ret = send(sd, (void*)nickname, dim, 0);
        if (ret == -1) {
            termina_con_errore(sd, "Errore nell'invio del nickname al server"); // Termina il client
        }

        dim = sizeof(stato_del_nickname);
        ret = recv(sd, &stato_del_nickname, dim, 0); // Riceve l'esito della registrazione del nickname
        if (ret == -1) {
            termina_con_errore(sd, "Errore nella ricezione della risposta dal server"); // Termina il client
        }
        stato_del_nickname = ntohl(stato_del_nickname); // Converte in formato host
        // A questo punto, stato_del_nickname sarà 0 se il nickname è errato o già registrato, altrimenti sarà 1
        // Se è 1, si esce dal ciclo, altrimenti si ripete il ciclo per inserire un nuovo nickname.
        if (stato_del_nickname == NICKNAME_ERRATO) {
            printf("Nickname già registrato: per favore, prova a usarne un altro\n");
        }
    }
    printf("\n");
    // Adesso ricevo prima il numero di temi, poi i titoli dei vari temi.
    dim = sizeof(numero_di_temi);
    ret = recv(sd, (void*)&numero_di_temi, dim, 0);
    if (ret == -1) {
        termina_con_errore(sd, "Errore nelle ricezione del numero di quiz");
    }
    numero_di_temi = ntohl(numero_di_temi); // Converto in formato host
    //printf("Numero di temi disponibili: %u\n", numero_di_temi);
    // Ora ricevo la lunghezza del buffer che contiene i temi
    ret = recv(sd, (void*)&dim, sizeof(dim), 0);
    if (ret == -1) {
        termina_con_errore(sd, "Errore nelle ricezione della lunghezza del buffer dei temi");
    }
    // Ora ricevo i temi come una stringa unica, separati da '\n'.
    memset(temi, 0, sizeof(temi)); // Inizializzo il buffer dei temi
    byte_ricevuti = 0; // Inizializzo il contatore dei byte ricevuti
    while (byte_ricevuti < dim) {
        ret = recv(sd, temi + byte_ricevuti, dim - byte_ricevuti, 0);
        if (ret <= 0) {
            termina_con_errore(sd, "Errore nella ricezione dei temi");
        }
        byte_ricevuti += ret;
    }
    // A questo punto, temi contiene tutti i titoli dei temi, separati da '\n'.
    while(1) {
        stampa_menu_temi(temi, numero_di_temi); // Stampa il menu dei temi
        printf("La tua scelta: ");
        scelta = scelta_numerica(1, numero_di_temi); // Scelta del tema
        // Invia la scelta del tema al server
        scelta = htonl(scelta); // Converto in formato network
        ret = send(sd, (void*)&scelta, sizeof(scelta), 0);
        if (ret == -1) {
            termina_con_errore(sd, "Errore nell'invio della scelta del tema al server");
        }
        // Ora il client può iniziare a giocare con il tema scelto.
    }
}