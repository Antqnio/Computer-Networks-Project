#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "include/costanti.h"
#include "include/stampa_delimitatore.h"
#include "include/send_recv_all.h"
#include "include/gestisci_sigpipe.h"

#define SERVER_CHIUSO 1 // Flag per indicare se il server è chiuso
#define SERVER_APERTO 0 // Flag per indicare se il server è aperto
#define QUIZ_TERMINATO 1 // Flag per indicare se il quiz è terminato
#define QUIZ_IN_CORSO 0 // Flag per indicare se il quiz è in corso

const char MESSAGGIO_SERVER_CHIUSO[] = "\nSocket lato server si è chiuso: mostro di nuovo il menù di avvio.\n\n";


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
        printf("Inserisci il tuo nickname (max %zu caratteri. Eventuali spazi saranno troncati):\n", size - 1);
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
        start = end + 1; // Passo al prossimo tema
        end = strchr(start, '\n'); // Trovo il prossimo '\n'
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


void gestisci_ritorno_recv_lato_client(int ret, int sd, const char* msg) {
    // Funzione per gestire il ritorno di recv e send lato client
    if (ret <= 0) {
        close(sd);
        if (ret == 0) {
            // Il server ha chiuso il socket remoto
            printf(MESSAGGIO_SERVER_CHIUSO);
        } else {
            printf("\n");
            perror(msg);
        }
    }
}

void gestisci_ritorno_send_lato_client(int ret, int sd, const char* msg) {
    if (ret < 0) {
        close(sd);
        printf("\n");
        perror(msg);
    }
}

uint8_t ricevi_ack(int sd, const char* errore_msg) {
    // Funzione per ricevere un ACK dal server (che vale 0 o 1)
    // Ritorna 2 se il server è chiuso. Ritorna 255 se c'è un errore.
    uint8_t ack;
    ssize_t ret = recv(sd, &ack, sizeof(ack), 0);
    if (ret <= 0) {
        close(sd);
        if (ret == 0) {
            return 2; // Connessione chiusa
        }
        if (ret == -1) {
            perror(errore_msg);
            return 255; // Errore nella ricezione
        }
    }
    return ack; // Ritorna l'ACK ricevuto
}

void stampa_punteggio_di_ogni_tema(const char* buffer, size_t size) {
    // Funzione per stampare i punteggi di ogni tema
    // Il buffer contiene gli informazioni sui punteggi dei temi, formattate come:
    // "<numero_tema>\n<punteggio>|<nickname>\n"
    // <numero_tema> è il numero del tema, seguito da un '\n'. Finché non si passa al prossimo tema,
    // non si troveranno altri '<numero_tema>\n'.
    printf("\n");
    const char* start = buffer;
    char *prossimo_newline = (char*)start, *prossimo_pipe; // Inizializzo prossimo_newline per poter entrare nel while
    while (prossimo_newline != NULL && start < buffer + size) {
        // Appena entro, trovo il primo numero di tema
        prossimo_newline = strchr(start, '\n'); // Trovo il prossimo '\n' (quello che segna il numero del tema)
        printf("Punteggio tema %.*s\n", (int)(prossimo_newline - start), start);
        // A questo punto, il prossimo carattere dopo il '\n' sarà il primo punteggio
        start += 2; // Passo al prossimo carattere dopo '\n'
        // A questo punto, devo stampare i giocatori con i rispettivi punteggi
        // Ho giocatori finché non trovo un '\n' che non è seguito da '|'
        // Quindi, mi basta vedere se, rispetto a start, è più vicino un '\n' o un '|'
        // - Se è più vicino un '\n', allora ho finito di stampare i giocatori per questo tema, quindi ho trovato un numero di tema
        // - Se è più vicino un '|', allora ho trovato il punteggio di un giocatore
        prossimo_newline = strchr(start, '\n'); // Trovo il prossimo '\n'
        prossimo_pipe = strchr(start, '|'); // Trovo il prossimo '|'
        while (prossimo_newline != NULL && prossimo_pipe != NULL && prossimo_pipe < prossimo_newline) {
            // Se il prossimo '|' è prima del prossimo '\n', allora
            // ho trovato un giocatore con il suo punteggio
            // Quindi, stampo il punteggio e il nickname del giocatore
            char punteggio[8];
            memset(punteggio, 0, sizeof(punteggio)); // Inizializzo il buffer del punteggio
            strncpy(punteggio, start, (size_t)(prossimo_pipe - start)); // Copio il punteggio
            prossimo_newline = strchr(prossimo_pipe + 1, '\n'); // Trovo il prossimo '\n' dopo il '|'
            printf("- %.*s %s\n", 
                (int)(prossimo_newline - (prossimo_pipe + 1)), // Lunghezza nickname
                prossimo_pipe + 1, // Nickname
                punteggio // Punteggio
            );
            start = prossimo_newline + 1; // Passo al prossimo carattere dopo '|'
            prossimo_newline = strchr(start, '\n'); // Trovo il prossimo '\n'
            prossimo_pipe = strchr(start, '|'); // Trovo il prossimo '|'
        }
    }
    printf("\n");
}


int main (int argc, char *argv[]) {
    char scelta; // Variabile per la scelta del menu iniziale e del tema di gioco
    int porta;
    char nickname[NICKNAME_MAX_LENGTH+1];
    char temi[1024]; // Buffer per i temi
    int sd;
    int stato_server = SERVER_CHIUSO; // Flag per verificare se il server è chiuso.
    ssize_t ret; // Variabile per gestire i ritorni delle funzioni di invio e ricezione
    ssize_t dim;
    struct sockaddr_in sv_addr; // Struttura per il server
    uint32_t stato_del_nickname = NICKNAME_ERRATO; // Flag per verificare se il nickname è già registrato
    uint32_t numero_di_temi;
    uint32_t dim_temi;
    if (argc != 2) {
        printf("Numero di argomenti non valido: riavviare il cliet con esattamente 2 argomenti\n");
        exit(EXIT_FAILURE);
    }
    porta = htons(atoi(argv[1])); // Converti la porta in formato di rete
    if (porta <= 0 || porta > 65535) {
        printf("Porta non valida: deve essere un numero tra 1 e 65535\n");
        exit(EXIT_FAILURE);
    }
    signal(SIGPIPE, gestisci_sigpipe); // Gestisco il segnale SIGPIPE per evitare che il client si chiuda in caso di errore di scrittura su un socket chiuso
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
            stato_del_nickname = NICKNAME_ERRATO; // Reset del flag per il nickname errato
            stato_server = SERVER_CHIUSO; // Reset del flag per il server chiuso
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
            ret = send_all(sd, (void*)&lunghezza_nickname_net, dim, gestisci_ritorno_send_lato_client, "Errore nell'invio della lunghezza del nickname al server");
            if (ret == 0 || ret == -1) {
                // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                // e provo a riconnettermi
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break; // Esco dal ciclo
            }
            // 2. Aspetto ACK per la lunghezza del nickname
            ack = ricevi_ack(sd, "Errore nella ricezione dell'ACK per la lunghezza del nickname");

            if (ack == 0) {
                printf("Lunghezza nickname non valida\n");
                continue; // Riprovo a inserire il nickname
            }
            else if (ack == 2 || ack == 255) {
                // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                // Se ack == 255, significa che c'è stato un errore nella ricezione dell'ACK, quindi esco dal ciclo
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break;
            }

            // 3. Invia il nickname al server
            dim = lunghezza_nickname;
            ret = send_all(sd, (void*)nickname, dim, gestisci_ritorno_send_lato_client, "Errore nell'invio del nickname al server");
            if (ret == 0 || ret == -1) {
                // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                // e provo a riconnettermi
                stato_server = SERVER_CHIUSO;
                break;
            }

            // 4. Riceve l'ACK della registrazione del nickname
            ack = ricevi_ack(sd, "Errore nella ricezione dello stato del nickname dal server");
            if (ack == 1) {
                printf("Nickname registrato con successo: %s\n", nickname);
                stato_del_nickname = NICKNAME_VALIDO; // Imposto il flag per il nickname valido
            }
            else if (ack == 0) {
                printf("Nickname già registrato: per favore, prova a usarne un altro\n");
                continue; // Riprovo a inserire il nickname
            }
            else /* if (ack == 2 || ack == -1) */ {
                // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                // Se ack == -1, significa che c'è stato un errore nella ricezione dell'ACK, quindi esco dal ciclo
                stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                break;
            }
        }
        
        // Se il server è chiuso, provo a riconnettermi
        if (stato_server == SERVER_CHIUSO) {
            printf("%s", MESSAGGIO_SERVER_CHIUSO);
            continue; // Ritorno all'inizio del ciclo per riconnettermi
        }
        printf("\n");
        
        // Adesso ricevo prima il numero di temi
        ret = recv_all(sd, (void*)&numero_di_temi, sizeof(numero_di_temi), gestisci_ritorno_recv_lato_client, "Errore nella ricezione del numero di temi");
        if (ret == 0 || ret == -1) {
            stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
            continue;
        }
        numero_di_temi = ntohl(numero_di_temi); // Converto in formato host
        printf("Numero di temi disponibili: %u\n", numero_di_temi);
        // Ora ricevo la lunghezza del buffer che contiene i temi
        ret = recv_all(sd, (void*)&dim_temi, sizeof(dim_temi), gestisci_ritorno_recv_lato_client, "Errore nella ricezione della lunghezza del buffer dei temi");
        if (ret == 0 || ret == -1) {
            stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
            close(sd);
            continue;
        }
        dim_temi = ntohl(dim_temi); // Converto in formato host
        // Ora ricevo i temi (o quiz) come una stringa unica, separati da '\n'.
        memset(temi, 0, sizeof(temi)); // Inizializzo il buffer dei temi
        ret = recv_all(sd, temi, dim_temi, gestisci_ritorno_recv_lato_client, "Errore nella ricezione dei temi");
        // A questo punto, temi contiene tutti i titoli dei temi, separati da '\n'.
        // Ora scelgo un tema e lo invio al server.
        while(1) {
            uint8_t ack = 0;
            uint8_t stato_quiz = QUIZ_IN_CORSO; // Flag per indicare se il quiz è terminato
            uint8_t domanda_non_ancora_ricevuta = 1; // Variabile per indicare se la domanda non è ancora stata ricevuta
            int c; // Usato per svuotare il buffer dello stdin
            int domande_ricevute = 0; // Contatore delle domande ricevute
            char tema_scelto[1024]; // Buffer per il tema scelto
            stampa_menu_temi(temi, numero_di_temi); // Stampa il menu dei temi
            while (ack != 1 && ack != 2 && ack != 255) {
                printf("La tua scelta: ");
                scelta = scelta_numerica(1, numero_di_temi); // Scelta del tema
                // Non serve convertire in network order perché è un uint8_t
                // Invio la scelta del tema al server
                ret = send_all(sd, (void*)&scelta, sizeof(scelta), gestisci_ritorno_send_lato_client, "Errore nell'invio della scelta del tema al server");
                if (ret == 0 || ret == -1) {
                    // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                    // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                    // e provo a riconnettermi
                    stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                    break;
                }
                // Riceve l'ACK per la scelta del tema
                ack = ricevi_ack(sd, "Errore nella ricezione dello stato della scelta del tema dal server");
                if (ack == 0) {
                    printf("Tema scelto già giocato: riprova\n");  // Riprovo a inserire il nickname
                }
                else if (ack == 2 || ack == 255) {
                    // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                    // Se ack == -1, significa che c'è stato un errore nella ricezione dell'ACK, quindi esco dal ciclo
                    stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                }
            }
            if (stato_server == SERVER_CHIUSO) {
                break; // Esco dal ciclo per riconnettermi
            }            
            ottieni_tema_scelto(temi, scelta, tema_scelto, sizeof(tema_scelto)); // Ottieni il tema scelto
            printf("\nQuiz - %s\n", tema_scelto);
            stampa_delimitatore();
            while ((c = getchar()) != '\n' && c != EOF); // Scarto tutto fino a newline (pulizia del buffer dello stdin)
            while(domande_ricevute < DOMANDE_PER_TEMA) {
                char domanda[1024]; // Buffer per la domanda
                char risposta_client[30]; // Buffer per la risposta
                char risposta_server[30]; // Buffer per la risposta del server
                size_t lunghezza_massima_risposta = 30; // Dimensione massima della risposta da leggere + 1 (per '\0)
                int prima_iterazione = 1; // Variabile per indicare se è la prima iterazione del ciclo do while sotto
                // Ricevo la dimensione della domanda dal server
                uint8_t dimensione_domanda;
                uint8_t dimensione_risposta_client;
                uint8_t dimensione_risposta_server;
                if (domanda_non_ancora_ricevuta) {
                    ret = recv_all(sd, &dimensione_domanda, sizeof(dimensione_domanda), gestisci_ritorno_recv_lato_client, "Errore nella ricezione della dimensione della domanda dal server");
                    if (ret == 0 || ret == -1) {
                        // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                        // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                        // e provo a riconnettermi
                        stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                        break;
                    }
                    // printf("Dimensione della domanda: %u\n", dimensione_domanda);
                    // Ricevo una domanda dal server
                    memset(domanda, 0, sizeof(domanda));
                    ret = recv_all(sd, domanda, dimensione_domanda, gestisci_ritorno_recv_lato_client, "Errore nella ricezione della domanda dal server");
                    if (ret == 0 || ret == -1) {
                        // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                        // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                        // e provo a riconnettermi
                        stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                        break;
                    }
                    // Stampa la domanda ricevuta e il prompt per la risposta)
                }
                printf("%s\n\nRisposta: ", domanda);
                // Leggo la risposta da tastiera
                memset(risposta_client, 0, sizeof(risposta_client));
                do {
                    if (!prima_iterazione) {
                        printf("Risposta non valida, riprova: ");
                    }
                    fflush(stdout); // Garantisce la stampa della printf() sopra
                    // fgets legge tutta la riga compresi spazi, anche se inserisco solo \n.
                    // scanf ignora gli spazi e i \n iniziali, e si ferma al primo spazio o \n.
                    if (fgets(risposta_client, lunghezza_massima_risposta, stdin) == NULL) {
                        // Ho ottenuto EOF o ho avuto un errore in lettura, quindi esco dal ciclo
                        break;
                    }
                    // Rimuovo il '\n' finale, se presente, e termino la stringa
                    risposta_client[strcspn(risposta_client, "\n")] = '\0';
                                    
                    // Controllo se la risposta è vuota
                    prima_iterazione = 0; // Faccio in modo che venga stampato il messaggio di risposta non valida
                } while (strlen(risposta_client) == 0);
                
                // Invia la dimensione della risposta al server
                dimensione_risposta_client = strlen(risposta_client);
                ret = send_all(sd, &dimensione_risposta_client, sizeof(dimensione_risposta_client), gestisci_ritorno_send_lato_client, "Errore nell'invio della dimensione della risposta al server");
                    if (ret == 0 || ret == -1) {
                    // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                    // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                    // e provo a riconnettermi
                    stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                    break;
                }
                // printf("Dimensione della risposta: %u\n", dimensione_risposta_client);
                // Non serve ack per la dimensione della risposta, perché il server rappresenta
                // la dimensione della risposta come un uint8_t, quindi non può essere > 255 o < 0.
                // Invia la risposta al server
                // printf("Risposta inviata: %s\n", risposta_client);
                ret = send_all(sd, (void*)risposta_client, strlen(risposta_client), gestisci_ritorno_send_lato_client, "Errore nell'invio della risposta al server");
                if (ret == 0 || ret == -1) {
                    // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                    // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                    // e provo a riconnettermi
                    stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                    break;
                }
                if (strcmp(risposta_client, "endquiz") == 0) {
                    // Se il client invia "endquiz", il server chiude la connessione
                    uint8_t ack = ricevi_ack(sd, "Errore nella ricezione dell'ACK per la fine del quiz");
                    if (ack == 0) {
                        // In realtà non dovrebbe mai arrivare qui, perché il server non dovrebbe inviare un ACK negativo per "endquiz"
                        printf("Quiz terminato dal server.\n");
                    }
                    else if (ack == 2) {
                        // Se ack == 2, significa che il server è chiuso, quindi esco dal ciclo
                        stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                    }
                    else /* if (ack == 1) */ {
                        printf("Quiz terminato dal client.\n");   
                        stato_quiz = QUIZ_TERMINATO; // Segno che il quiz è terminato
                    }
                    printf("\n");
                    break; // Esco dal ciclo delle domande
                }
                if (strcmp(risposta_client, "show score") == 0) {
                    // Se il client invia "show score", il server invia il punteggio di ogni tema
                    // Ricevo la dimensione della stringa
                    uint32_t dimensione_risposta_server;
                    ret = recv_all(sd, &dimensione_risposta_server, sizeof(dimensione_risposta_server), gestisci_ritorno_recv_lato_client, "Errore nella ricezione della dimensione della risposta del server");
                    if (ret == 0 || ret == -1) {
                        // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                        // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                        // e provo a riconnettermi
                        stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                        break;
                    }
                    dimensione_risposta_server = ntohl(dimensione_risposta_server); // Converto in formato host
                    // Ricevo la risposta del server (il punteggio di ogni tema con relativi nickname)
                    memset(risposta_server, 0, sizeof(risposta_server));
                    ret = recv_all(sd, (void*)risposta_server, dimensione_risposta_server, gestisci_ritorno_recv_lato_client, "Errore nella ricezione della risposta del server");
                    if (ret == 0 || ret == -1) {
                        // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                        // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                        // e provo a riconnettermi
                        stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                        break;
                    }
                    risposta_server[dimensione_risposta_server] = '\0'; // Aggiungo il terminatore alla risposta del server
                    stampa_punteggio_di_ogni_tema(risposta_server, dimensione_risposta_server); // Stampa i punteggi dei temi
                    domanda_non_ancora_ricevuta = 0; // Segno che il client ha già ricevuto la domanda
                    continue; // Ritorno all'inizio del ciclo per ricevere una nuova domanda
                }
                // Ricevo la dimensione della risposta dal server
                ret = recv_all(sd, &dimensione_risposta_server, sizeof(dimensione_risposta_server), gestisci_ritorno_recv_lato_client, "Errore nella ricezione della dimensione della risposta dal server");
                if (ret == 0 || ret == -1) {
                    // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                    // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                    // e provo a riconnettermi
                    stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                    break;
                }
                // printf("Dimensione della risposta del server: %u\n", dimensione_risposta_server);
                // Ricevo la risposta dal server (contenente l'esito della risposta)
                memset(risposta_server, 0, sizeof(risposta_server));
                ret = recv_all(sd, (void*)risposta_server, dimensione_risposta_server, gestisci_ritorno_recv_lato_client, "Errore nella ricezione dell'esito della risposta del server");
                if (ret == 0 || ret == -1) {
                    // Se ret == 0, significa che il socket si è chiuso, quindi esco dal ciclo
                    // Se ret == -1, significa che c'è stato un errore nell'invio, quindi esco dal ciclo
                    // e provo a riconnettermi
                    stato_server = SERVER_CHIUSO; // Imposto il flag per il server chiuso
                    break;
                }
                risposta_server[dimensione_risposta_server] = '\0'; // Aggiungo il terminatore alla risposta del server
                printf("%s\n\n", risposta_server);
                ++domande_ricevute;
                domanda_non_ancora_ricevuta = 1; // Segno che il client dovrà ricevere una nuova domanda
            }
            if (stato_server == SERVER_CHIUSO || stato_quiz == QUIZ_TERMINATO) {
                break; // Esco dal ciclo delle domande
            }
        }
        if (stato_server == SERVER_CHIUSO) {
            printf("%s", MESSAGGIO_SERVER_CHIUSO);
            continue; // Ritorno all'inizio del ciclo per riconnettermi. Evito di fare close(sd) perché il socket è già chiuso
            // Infatti, setto stato_server a SERVER_CHIUSO quando ricevo un ACK negativo (con la
            // ricevi_ack()) o si chiama gestisci_ritorno_recv_send_lato_client() con ret == 0 o -1.
            // Entrambe le funzioni già chiudono il socket.
        }
        close(sd); // Chiudo il socket
    }
}