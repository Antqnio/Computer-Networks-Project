#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "include/costanti.h"
#include "include/stampa_delimitatore.h"
#include "include/comandi.h"
#include "include/send_recv_all.h"


#define PORTA 4242
#define BACKLOG_SIZE 10
#define NUMERO_QUIZ 5
#define LUNGHEZZA_MASSIMA_QUIZ 10

struct Giocatore {
    int socket;
    char nickname[NICKNAME_MAX_LENGTH];
    struct Giocatore* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Giocatore* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};

struct Giocatore* albero_giocatori = NULL; // Lista dei giocatori connessi


const char QUIZ_DISPONIBILI[NUMERO_QUIZ][LUNGHEZZA_MASSIMA_QUIZ] = {"Geografia", "Storia", "Sport", "Cinema", "Arte"};

int ottieni_partecipanti(struct Giocatore* curr) {
    // Funzione per ottenere il numero di partecipanti
    if (curr == NULL) {
        return 0; // Se la lista è vuota, ritorna 0
    }
    return 1 + ottieni_partecipanti(curr->left) + ottieni_partecipanti(curr->right);
}

void stampa_partecipanti(struct Giocatore* curr) {
    // Funzione per stampare i partecipanti in ordine alfabetico (visita inorder)
    if (curr == NULL) {
        return; // Se la lista è vuota, ritorna
    }
    stampa_partecipanti(curr->left); // Stampa il sottoalbero sinistro
    printf("- %s\n", curr->nickname);
    stampa_partecipanti(curr->right); // Stampa il sottoalbero destro
}

void stampa_interfaccia() {
    stampa_delimitatore();
    printf("Temi:\n");
    for (int i = 0; i < NUMERO_QUIZ; ++i) {
        printf("%d - %s\n", i + 1, QUIZ_DISPONIBILI[i]);
    }
    stampa_delimitatore();
    printf("\n");

    printf("Partecipanti (%d):\n", ottieni_partecipanti(albero_giocatori));
    stampa_partecipanti(albero_giocatori); // Funzione per stampare i partecipanti
    printf("\n\n");
    printf("------\n");
}

void inserisci_giocatore_ricorsivo(struct Giocatore* curr, struct Giocatore nuovoGiocatore) {
    // Funzione ricorsiva per inserire un giocatore nell'albero binario di ricerca
    if (strcmp(nuovoGiocatore.nickname, curr->nickname) < 0) {
        // Se il nuovo giocatore è "minore" del corrente, va a sinistra
        if (curr->left == NULL) {
            curr->left = malloc(sizeof(struct Giocatore));
            *curr->left = nuovoGiocatore;
            curr->left->left = NULL;
            curr->left->right = NULL;
        } else {
            inserisci_giocatore_ricorsivo(curr->left, nuovoGiocatore);
        }
    } else {
        // Altrimenti va a destra
        if (curr->right == NULL) {
            curr->right = malloc(sizeof(struct Giocatore));
            *curr->right = nuovoGiocatore;
            curr->right->left = NULL;
            curr->right->right = NULL;
        } else {
            inserisci_giocatore_ricorsivo(curr->right, nuovoGiocatore);
        }
    }
}

void inserisci_giocatore(struct Giocatore nuovoGiocatore) {
    // Funzione per aggiungere un giocatore alla lista dei giocatori
    if (albero_giocatori == NULL) {
        // Se l'albero è vuoto, inizializza l'albero con il nuovo giocatore
        albero_giocatori = malloc(sizeof(struct Giocatore));
        *albero_giocatori = nuovoGiocatore;
    } else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine alfabetico di nickname
        inserisci_giocatore_ricorsivo(albero_giocatori, nuovoGiocatore);
    }
}

int nickname_gia_registrato(const char* nickname, struct Giocatore* curr) {
    // Funzione per verificare se il nickname è già registrato
    if (curr == NULL) {
        return 0; // Se l'albero è vuoto, il nickname non è registrato
    }
    if (strcmp(curr->nickname, nickname) == 0) {
        return 1; // Il nickname è già registrato
    }
    else if (strcmp(nickname, curr->nickname) < 0) {
        // Se il nickname è "minore", cerca nel sottoalbero sinistro
        return nickname_gia_registrato(nickname, curr->left);
    } else {
        // Altrimenti cerca nel sottoalbero destro
        return nickname_gia_registrato(nickname, curr->right);
    }
}

void gestisci_ritorno_recv_send_lato_server(int ret, int sd, const char* msg) {
    // Funzione per gestire il ritorno di recv
    if (ret <= 0) {
        if (ret == 0) {
            printf("Socket lato client chiuso.\n");
        } else {
            perror(msg);
        }
        // Chiudo il socket associato al client sia nel caso di errore che di chiusura
        close(sd);
        pthread_exit(NULL); // Termina il thread
    }
}

void invia_ack(int sd, uint8_t ack) {
    // Funzione per inviare un ACK al client
    ssize_t ret = send(sd, &ack, sizeof(ack), 0);
    if (ret <= 0) {
        perror("Errore nell'invio dell'ACK al client");
        close(sd);
        pthread_exit(NULL); // Termina il thread
    }
}

struct Giocatore crea_giocatore(int cl_sd) {
    char nickname[NICKNAME_MAX_LENGTH];
    uint32_t stato_del_nickname = 0;
    // Ricevi il nickname dal client
    while (stato_del_nickname == NICKNAME_ERRATO) {
        memset(nickname, 0, sizeof(nickname)); // Inizializza il nickname
        // Ricevi la lunghezza del nickname
        uint32_t lunghezza_nickname = 0, byte_ricevuti = 0;
        uint8_t ack;
        recv_all(cl_sd, &lunghezza_nickname, sizeof(lunghezza_nickname), gestisci_ritorno_recv_send_lato_server, "Errore nella ricezione della lunghezza del nickname");
        // printf("Lunghezza nickname ricevuta: %d\n", lunghezza_nickname);
        lunghezza_nickname = ntohl(lunghezza_nickname); // Converto in formato host
        // Ricevi il nickname dal client
        if (lunghezza_nickname == 0 || lunghezza_nickname >= NICKNAME_MAX_LENGTH) {
            // Se la lunghezza del nickname è 0 o troppo lunga, gestisci l'errore
            invia_ack(cl_sd, 0); // Invia un ACK negativo al client
            continue;
        }
        // Lunghezza valida: informo il client che può inviare il nickname
        invia_ack(cl_sd, 1); // Invia un ACK positivo al client
        // Faccio in modo di ricevere esattamente un numero di byte pari alla lunghezza del nickname
        recv_all(cl_sd, nickname, lunghezza_nickname, gestisci_ritorno_recv_send_lato_server, "Errore nella ricezione del nickname");
        // Controlla se il nickname è valido
        if (nickname_gia_registrato(nickname, albero_giocatori)) {
            // printf("Nickname errato o già registrato: %s\n", nickname);
            // Invia un messaggio di errore al client, così può riprovare a inviare un nickname valido
            invia_ack(cl_sd, 0); // Invia un ACK negativo al client
            continue;
        }
        else {
            // printf("Nickname valido: %s\n", nickname);
            stato_del_nickname = NICKNAME_VALIDO;
            invia_ack(cl_sd, 1); // Invia un ACK positivo al client
            continue;
        }
    }
    // Se siamo qui, il nickname è valido, quindi creiamo un nuovo Giocatore
    struct Giocatore nuovoGiocatore;
    nuovoGiocatore.socket = cl_sd;
    memset(nuovoGiocatore.nickname, 0, sizeof(nuovoGiocatore.nickname)); // Pulizia del campo nickname
    strcpy(nuovoGiocatore.nickname, nickname); // Copia il nickname nel nuovo giocatore
    nuovoGiocatore.left  = NULL;
    nuovoGiocatore.right = NULL;
    inserisci_giocatore(nuovoGiocatore); // Aggiungi il nuovo giocatore alla lista dei giocatori
    return nuovoGiocatore;

}

void invia_quiz_disponibili(struct Giocatore giocatore) {
    ssize_t ret;
    char messaggio_di_errore[256];
    // Prima invio il numero di quiz disponibili
    uint32_t numero_di_quiz_disponibili = NUMERO_QUIZ; // Numero di quiz disponibili
    uint32_t numero_di_quiz_disponibili_net = htonl(numero_di_quiz_disponibili); // Converto in formato di rete
    size_t dimensione_messaggio = sizeof(numero_di_quiz_disponibili);
    // Scrivo il messaggio di errore
    memset(messaggio_di_errore, 0, sizeof(messaggio_di_errore));
    snprintf(messaggio_di_errore, sizeof(messaggio_di_errore), "Errore nell'invio del numero dei quiz al client %s: ", giocatore.nickname);
    ret = send_all(giocatore.socket, (void*)&numero_di_quiz_disponibili_net, dimensione_messaggio, gestisci_ritorno_recv_send_lato_server, messaggio_di_errore);

    // Ora invio i temi come una stringa unica il cui separatore è '\n'.
    size_t dim = 1024;
    char buffer[dim];
    memset(buffer, 0, dim);
    for (int i = 0; i < NUMERO_QUIZ; ++i) {
        strcat(buffer, QUIZ_DISPONIBILI[i]);
        strcat(buffer, "\n"); // Uso "\n" come separatore
    }
    // Prima di inviare i temi, calcolo la lunghezza del buffer e la converto in formato di rete,
    // per poi inviarla al client.
    ret = send_all(giocatore.socket, (void*)&dim, sizeof(dim), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della lunghezza del buffer dei temi al client");

    // Ora invio il buffer contenente i temi
    ret = send_all(giocatore.socket, (void*)buffer, dim, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio dei temi al client");

}

char ricevi_quiz_scelto(struct Giocatore giocatore) {
    ssize_t ret;
    uint8_t quiz_scelto = 0;
    while(quiz_scelto < 1 || quiz_scelto > NUMERO_QUIZ) {
        // Ricevo il quiz scelto dal client (uso recv perhé è un uint8_t)
        ret = recv(giocatore.socket, (void*)&quiz_scelto, sizeof(quiz_scelto), 0);
        gestisci_ritorno_recv_send_lato_server(ret, giocatore.socket, "Errore nella ricezione del quiz scelto dal client");
        if (quiz_scelto < 1 || quiz_scelto > NUMERO_QUIZ) {
            invia_ack(giocatore.socket, 0); // Invia un ACK negativo al client
        }
        else {
            invia_ack(giocatore.socket, 1); // Invia un ACK positivo al client
            printf("Il client %s ha scelto il quiz: %s\n", giocatore.nickname, QUIZ_DISPONIBILI[quiz_scelto - 1]);
        }
    }
    return quiz_scelto; // Ritorno il numero del quiz scelto
}

void* gestisci_connessione(void* arg) {
    int cl_sd = *(int*)arg;
    int ret;
    char quiz_scelto;
    char messaggio = 0; // Variabile per gestire i messaggi ricevuti dal client. Inizializzo a 0 per "pulizia".
    struct Giocatore giocatore = crea_giocatore(cl_sd);
    stampa_interfaccia(); // Stampa l'interfaccia grafica del server
    invia_quiz_disponibili(giocatore); // Funzione per inviare i quiz disponibili al giocatore
    quiz_scelto = ricevi_quiz_scelto(giocatore); // Funzione per ricevere il quiz scelto dal giocatore
    // Adesso il client può iniziare a giocare con il quiz scelto.
    while (1) {
        // Invio la prima domanda del quiz scelto al client
        
        
    }
    gestisci_ritorno_recv(ret, cl_sd, "Errore nella ricezione dei dati dal client");

    close(cl_sd);
    pthread_exit(NULL); // Termina il thread
}




int main () {
    int ret, sd, cl_sd;
    socklen_t len;
    pthread_t thread_id;
    struct sockaddr_in my_addr, cl_addr;
    stampa_interfaccia(); // Funzione per stampare l'interfaccia grafica
    /* Creazione socket */
    sd = socket(AF_INET, SOCK_STREAM, 0);
    /* Creazione indirizzo */
    memset(&my_addr, 0, sizeof(my_addr)); // Pulizia
    my_addr.sin_family = AF_INET ;
    my_addr.sin_port = htons(PORTA);
    inet_pton(AF_INET, SERVER_IP, &my_addr.sin_addr);
    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if (ret < 0) {
        perror("Errore nel bind");
        exit(EXIT_FAILURE);
    }
    ret = listen(sd, BACKLOG_SIZE);
    if (ret < 0) {
        perror("Errore nel listen");
        exit(EXIT_FAILURE);
    }
    len = sizeof(cl_addr);
    while(1) {
        cl_sd = accept(sd, (struct sockaddr*)&cl_addr, &len);
        if (cl_sd < 0) {
            perror("Errore nell'accept");
            exit(EXIT_FAILURE);
        }
        ret = pthread_create(&thread_id, NULL, gestisci_connessione, (void*)&cl_sd);
        if (ret != 0) {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }
        
    }
}