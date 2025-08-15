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
#include "include/send_recv_all.h"

#define SERVER_CHIUSO 1 // Flag per indicare se il server è chiuso
#define SERVER_APERTO 0 // Flag per indicare se il server è aperto

uint8_t scelta_numerica(int min, int max) {
    // Funzione per leggere una scelta numerica da tastiera
    // Uso un uint8_t perché un byte è sufficiente per le scelte del menu
    int scelta;
    while (scanf("%d", &scelta) == -1 || scelta < min || scelta > max) { // Controlla se la scelta è valida
        // Se scanf fallisce (per esempio, se provo a inserire un char da tastiera) o la scelta non è valida, stampa un messaggio di erro
        printf("Scelta non valida, riprova:\n");
        scanf("%d", &scelta);
        // Pulisco il buffer dell'input
        while (getchar() != '\n'); // leggo e scarto tutto fino a fine riga
    }
    return (uint8_t)scelta; // Ritorna la scelta valida
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
    // numero_di_temi non sarebbbe necessario, ma lo uso per sicurezza
    printf("Quiz disponibili:\n");
    stampa_delimitatore();
    // Stampa i temi separati da '\n'
    const char* start = temi;
    const char* end = strchr(start, '\n'); // Trova il primo '\n'
    int i = 1;
    while (end != NULL && i <= numero_di_temi) {
        // (int)(end - start) calcola la lunghezza del tema: è il numero di caratteri tra start e end,
        // con end escluso.
        printf("%d - %.*s\n", i, (int)(end - start), start);
        start = end + 1; // Passa al prossimo tema
        end = strchr(start, '\n'); // Trova il prossimo '\n'
        i++;
    }
    stampa_delimitatore();
}

void ottieni_tema_scelto(const char* temi, uint8_t scelta, char* tema_scelto, size_t size) {
    // Scorri la stringa fino al (scelta-1)-esimo '\n'
    const char* start = temi;
    for (int i = 1; i < scelta; ++i) {
        start = strchr(start, '\n'); // Trova il prossimo '\n'
        if (!start) {
            // Se non c'è un altro '\n', significa che la scelta è fuori dai temi disponibili
            fprintf(stderr, "Tema scelto non trovato\n");
            tema_scelto[0] = '\0';
            return;
        }
        start++; // Vai al carattere dopo '\n'
    }
    const char* end = strchr(start, '\n');
    if (!end) {
        // Se non c'è un altro '\n', prendo i caratteri fino alla fine della stringa
        end = temi + strlen(temi);
    }
    size_t length = end - start;
    if (length >= size) {
        // Se la lunghezza del tema scelto è maggiore della dimensione del buffer, lo tronco
        // Questo caso non dovrebbe mai verificarsi.
        length = size - 1;
    }
    strncpy(tema_scelto, start, length);
    tema_scelto[length] = '\0';
}


void gestisci_ritorno_recv_send_lato_client(int ret, int sd, const char* msg) {
    // Funzione per gestire il ritorno di recv lato client
    if (ret <= 0) {
        close(sd);
        if (ret == 0) {
            printf("Socket lato server chiuso.\n");
        } else {
            perror(msg);
            exit(EXIT_FAILURE); // Termina il client
        }
    }
}

uint8_t ricevi_ack(int sd, const char* errore_msg) {
    // Funzione per ricevere un ACK dal server
    // Ritorna 2 se il server è chiuso. Termina il client se c'è un errore.
    uint8_t ack;
    ssize_t ret = recv(sd, &ack, sizeof(ack), 0);
    if (ret <= 0) {
        close(sd);
        if (ret == 0) {
            printf("Socket lato server chiuso.\n");
            return 2; // Connessione chiusa
        }
        if (ret == -1) {
            perror(errore_msg);
            exit(EXIT_FAILURE); // Termina il client
        }
    }
    return ack; // Ritorna l'ACK ricevuto
}

int main (int argc, char *argv[]) {
    char scelta; // Variabile per la scelta del menu iniziale e del tema di gioco
    int porta;
    char nickname[NICKNAME_MAX_LENGTH];
    char temi[1024]; // Buffer per i temi
    int sd;
    int stato_server = SERVER_CHIUSO; // Flag per verificare se il server è chiuso.
    ssize_t ret; // Variabile per gestire i ritorni delle funzioni di invio e ricezione
    ssize_t dim;
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
    while(1) {
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
            else {
                stato_server = SERVER_APERTO; // Reset del flag per il server chiuso
            }
        }
        // Ora che siamo connessi, possiamo inviare il nickname:
        while (stato_del_nickname == NICKNAME_ERRATO) {
            uint32_t lunghezza_nickname; // Lunghezza del nickname da inviare al server
            uint32_t lunghezza_nickname_net; // Lunghezza nickname in network order
            uint8_t ack;
            // Inserimento del nickname (sarà troncato se supera la lunghezza massima, quindi sarà semppre valido)
            mostra_menu_nickname(nickname, sizeof(nickname));
            // Invia la lunghezza del nickname al server
            // printf("Nickname da inviare: %s\n", nickname);
            lunghezza_nickname = strlen(nickname); // Invio al server la quantita di dati
            // printf("Lunghezza nickname: %u\n", lunghezza_nickname);
            lunghezza_nickname_net = htonl(lunghezza_nickname); // Converto in formato network
            // printf("Lunghezza nickname da inviare: %u\n", lunghezza_nickname_net);
    
            // 1. Invia la lunghezza del nickname (quindi quanti byte il server si aspetta)
            dim = sizeof(lunghezza_nickname_net);
            ret = send_all(sd, (void*)&lunghezza_nickname_net, dim, gestisci_ritorno_recv_send_lato_client, "Errore nell'invio della lunghezza del nickname al server");
            if (ret < dim) {
                // Se ret == -1, significa che c'è stato un errore nell'invio, ma questo caso è già gestito dalla funzione send_all
                // Quindi, se ret < dim, significa che non ho inviato tutti i byte che mi aspettavo, quindi che il socket lato server è chiuso
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break; // Esco dal ciclo
            }
            // 2. Aspetto ACK per la lunghezza del nickname
            ack = ricevi_ack(sd, "Errore nella ricezione dell'ACK per la lunghezza del nickname");

            if (ack == 0) {
                printf("Lunghezza nickname non valida\n");
                continue; // Riprovo a inserire il nickname
            }
            else if (ack == 2) {
                // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break;
            }

            // 3. Invia il nickname al server
            dim = lunghezza_nickname;
            ret = send_all(sd, (void*)nickname, dim, gestisci_ritorno_recv_send_lato_client, "Errore nell'invio del nickname al server");
            if (ret < dim) {
                stato_server = SERVER_CHIUSO;
                break;
            }

            // 4. Riceve l'ACK della registrazione del nickname
            ack = ricevi_ack(sd, "Errore nella ricezione dello stato del nickname dal server");
            if (ack == 1) {
                printf("Nickname registrato con successo: %s\n", nickname);
                stato_del_nickname = NICKNAME_VALIDO; // Imposto il flag per il nickname valido
            } else if (ack == 0) {
                printf("Nickname già registrato: per favore, prova a usarne un altro\n");
                continue; // Riprovo a inserire il nickname
            }
            else if (ack == 2) {
                // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break;
            }
        }
        
        // Se il server è chiuso, provo a riconnettermi
        if (stato_server == SERVER_CHIUSO) {
            printf("Il server è chiuso: provo a riconettermi.\n");
            break;
        }
        printf("\n");
        // Adesso ricevo prima il numero di temi, poi i titoli dei vari temi.
        dim = sizeof(numero_di_temi);
        ret = recv_all(sd, (void*)&numero_di_temi, dim, gestisci_ritorno_recv_send_lato_client, "Errore nella ricezione del numero di temi");
        if (ret == 0) {
            stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
            continue;
        }
        numero_di_temi = ntohl(numero_di_temi); // Converto in formato host
        printf("Numero di temi disponibili: %u\n", numero_di_temi);
        // Ora ricevo la lunghezza del buffer che contiene i temi
        ret = recv_all(sd, (void*)&dim, sizeof(dim), gestisci_ritorno_recv_send_lato_client, "Errore nella ricezione della lunghezza del buffer dei temi");
        if (ret == 0) {
            stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
            continue;
        }
        // Ora ricevo i temi come una stringa unica, separati da '\n'.
        memset(temi, 0, sizeof(temi)); // Inizializzo il buffer dei temi
        recv_all(sd, temi, dim, gestisci_ritorno_recv_send_lato_client, "Errore nella ricezione dei temi");
        // A questo punto, temi contiene tutti i titoli dei temi, separati da '\n'.
        // Ora scelgo un tema e lo invio al server.
        while(1) {
            uint8_t ack;
            char tema_scelto[1024]; // Buffer per il tema scelto
            stampa_menu_temi(temi, numero_di_temi); // Stampa il menu dei temi
            printf("La tua scelta: ");
            scelta = scelta_numerica(1, numero_di_temi); // Scelta del tema
            // Non serve convertire in network order perché è un uint8_t
            // Invia la scelta del tema al server
            ret = send(sd, (void*)&scelta, sizeof(scelta), 0);
            gestisci_ritorno_recv_send_lato_client(ret, sd, "Errore nell'invio della scelta del tema al server");
            // Riceve l'ACK per la scelta del tema
            ack = ricevi_ack(sd, "Errore nella ricezione dello stato della scelta del tema dal server");
            if (ack == 0) {
                printf("Tema scelto non valido: riprova:\n");
                continue; // Riprovo a inserire il nickname
            }
            else if (ack == 2) {
                // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break;
            }
            ottieni_tema_scelto(temi, scelta, tema_scelto, sizeof(tema_scelto)); // Ottieni il tema scelto
            printf("\nQuiz - %s\n", tema_scelto);
            stampa_delimitatore();
            while(1) {
                char domanda[1024]; // Buffer per la domanda
                char risposta[30]; // Buffer per la risposta
                char format[50]; // Buffer per il formato di scanf
                char risposta_server[30]; // Buffer per la risposta del server
                size_t size_scanf = 30; // Dimensione massima della risposta da leggere + 1 (per '\0)
                int prima_iterazione = 1;
                // Ricevo la dimensione della domanda dal server
                uint8_t dimensione_domanda;
                ret = recv(sd, &dimensione_domanda, sizeof(dimensione_domanda), 0);
                gestisci_ritorno_recv_send_lato_client(ret, sd, "Errore nella ricezione della dimensione della domanda dal server");
                // Ricevo una domanda dal server
                printf("Dimensione della domanda: %u\n", dimensione_domanda);
                memset(domanda, 0, sizeof(domanda)); // Inizializza il buffer della domanda
                recv_all(sd, domanda, dimensione_domanda, gestisci_ritorno_recv_send_lato_client, "Errore nella ricezione della domanda dal server");
                // Stampa la domanda ricevuta
                memset(risposta, 0, sizeof(risposta)); // Inizializza il buffer della risposta
                printf("%s\n\nRisposta: ", domanda);
                while (strlen(risposta) == 0) {
                    if (!prima_iterazione) {
                        printf("Risposta non valida, riprova:\n");
                    }
                    fflush(stdout); // Garantisce la stampa della printf() sopra
                    snprintf(format, sizeof(format), "%%%zus", size_scanf - 1); // Permette risposte di massimo 29 caratteri + '\0'
                    scanf(format, risposta); // Legge il nickname
                    // Controlla se la risposta è vuota
                    prima_iterazione = 0; // Faccio in modo che venga stampato il messaggio di risposta non valida
                }
                // Invia la dimensione della risposta al server
                uint8_t dimensione_risposta = strlen(risposta);
                ret = send(sd, &dimensione_risposta, sizeof(dimensione_risposta), 0);
                gestisci_ritorno_recv_send_lato_client(ret, sd, "Errore nell'invio della dimensione della risposta al server");
                // Non serve ack per la dimensione della risposta, perché il server rappresenta
                // la dimensione della risposta come un uint8_t, quindi non può essere > 255 o < 0.
                // Invia la risposta al server
                send_all(sd, (void*)risposta, strlen(risposta), gestisci_ritorno_recv_send_lato_client, "Errore nell'invio della risposta al server");
                // Ricevo la risposta dal server (contenente l'esito della risposta)
                memset(risposta_server, 0, sizeof(risposta_server));
                recv_all(sd, (void*)risposta_server, DIMENSIONE_RISPOSTA, gestisci_ritorno_recv_send_lato_client, "Errore nella ricezione dell'esito della risposta del server");
                printf("%s\n", risposta_server);
            }
        }
    }
}