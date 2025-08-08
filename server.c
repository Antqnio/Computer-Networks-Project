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


const char QUIZ_DISPONIBILI[NUMERO_QUIZ][LUNGHEZZA_MASSIMA_QUIZ] = {"Geografia", "Storia", "Sport", "Scienze", "Arte"};

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

void invia_stato_nickname(int client_socket, uint32_t stato_del_nickname) {
    uint32_t stato_del_nickname_net = htonl(stato_del_nickname); // Converto in formato di rete
    // Invia un messaggio di conferma al client
    send(client_socket, (void*)&stato_del_nickname_net, sizeof(stato_del_nickname_net), 0);
}

struct Giocatore crea_giocatore(int cl_sd) {
    char nickname[NICKNAME_MAX_LENGTH];
    uint32_t stato_del_nickname = 0;
    memset(nickname, 0, sizeof(nickname)); // Inizializza il nickname
    // Ricevi il nickname dal client
    while (stato_del_nickname == NICKNAME_ERRATO) {
        // Ricevi la lunghezza del nickname
        int lunghezza_nickname = 0, byte_ricevuti = 0;
        int ret = recv(cl_sd, &lunghezza_nickname, sizeof(lunghezza_nickname), 0);
        printf("Lunghezza nickname ricevuta: %d\n", lunghezza_nickname);
        if (ret <= 0) {
            perror("Errore nella ricezione della lunghezza del nickname");
            close(cl_sd);
            pthread_exit(NULL); // Termina il thread
        }
        // Ricevi il nickname dal client
        // Faccio in modo di ricevere esattamente un numero di byte pari alla lunghezza del nickname
        lunghezza_nickname = ntohl(lunghezza_nickname); // Converto in formato host
        printf("Lunghezza nickname ricevuta: %d\n", lunghezza_nickname);
        while(byte_ricevuti < lunghezza_nickname) {
            int ret = recv(cl_sd, nickname + byte_ricevuti, lunghezza_nickname - byte_ricevuti, 0);
            if (ret <= 0) {
                perror("Errore nella ricezione del nickname");
                close(cl_sd);
                pthread_exit(NULL); // Termina il thread
            }
            byte_ricevuti += ret;
            printf("Ricevuti %d byte del nickname: %s\n", byte_ricevuti, nickname);
            printf("Lunghezza nickname: %d\n", lunghezza_nickname);
        }
        // Controlla se il nickname è valido
        if (strlen(nickname) == 0 || strlen(nickname) >= NICKNAME_MAX_LENGTH || nickname_gia_registrato(nickname, albero_giocatori)) {
            printf("Nickname errato o già registrato: %s\n", nickname);
            // Invia un messaggio di errore al client, così può riprovare a inviare un nickname valido
            stato_del_nickname = NICKNAME_ERRATO;
            invia_stato_nickname(cl_sd, NICKNAME_ERRATO);
            memset(nickname, 0, sizeof(nickname)); // Resetta il nickname
        }
        else {
            printf("Nickname valido: %s\n", nickname);
            stato_del_nickname = NICKNAME_VALIDO;
            invia_stato_nickname(cl_sd, NICKNAME_VALIDO);
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

void invia_quiz_disponibili(int client_socket, struct Giocatore giocatore) {
    int ret;
    // Prima invio il numero di quiz disponibili
    uint32_t numero_di_quiz_disponibili = NUMERO_QUIZ; // Numero di quiz disponibili
    uint32_t numero_di_quiz_disponibili_net = htonl(numero_di_quiz_disponibili); // Converto in formato di rete
    size_t dimensione_messaggio = sizeof(numero_di_quiz_disponibili);
    ret = send(client_socket, (void*)&numero_di_quiz_disponibili_net, dimensione_messaggio, 0);
    if (ret == -1) {
        fprintf(stderr, "Errore nell'invio del numero dei quiz al client %s: ", giocatore.nickname);
        perror(""); // Stampo solo la descrizione dell'errore
        pthread_exit(NULL);
    }
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
    send(client_socket, (void*)&dim, sizeof(dim), 0); // Invio la dimensione del buffer
    // Ora invio il buffer contenente i temi
    send(client_socket, (void*)buffer, dim, 0);
}

void* gestisci_connessione(void* arg) {
    int cl_sd = *(int*)arg;
    int ret;
    char messaggio = 0; // Variabile per gestire i messaggi ricevuti dal client
    struct Giocatore giocatore = crea_giocatore(cl_sd);
    stampa_interfaccia(); // Stampa l'interfaccia grafica del server
    invia_quiz_disponibili(cl_sd, giocatore); // Funzione per inviare i quiz disponibili al giocatore
    while ((ret = recv(cl_sd, (void*)&messaggio, sizeof(messaggio), 0) > 0)) {
        // Il server rimane in attesa di ricevere dati dal client.
        // In un'applicazione reale, qui si gestirebbero le richieste del client.
        // Per ora, il server non fa nulla e continua a gestire la connessione.
        
    }
    if (ret == -1) {
        perror("Errore nella ricezione dei dati dal client");
    } else /* if (ret == 0) */ {
        printf("Il client %s si è disconnesso.\n", giocatore.nickname);
    }

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
    ret = listen(sd, BACKLOG_SIZE);
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