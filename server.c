#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "include/constants.h"
#include "include/stampa_delimitatore.h"

#define PORTA 4242
#define BACKLOG_SIZE 10

struct Giocatore {
    int socket;
    char nickname[NICKNAME_MAX_LENGTH];
    struct Giocatore* next; // Puntatore al prossimo giocatore nella lista
};

struct Giocatore* lista_giocatori = NULL; // Lista dei giocatori connessi

void inserisciGiocatore(struct Giocatore nuovoGiocatore) {
    // Funzione per aggiungere un giocatore alla lista
    if (lista_giocatori == NULL) {
        lista_giocatori = malloc(sizeof(struct Giocatore));
        *lista_giocatori = nuovoGiocatore;
    } else {
        // Aggiungi il nuovo giocatore alla fine della lista
        struct Giocatore* curr;
        for (curr = lista_giocatori; curr->next != NULL; curr = curr->next) {
            // Trova l'ultimo giocatore
        }
        curr->next = malloc(sizeof(struct Giocatore));
        *curr->next = nuovoGiocatore;
        curr->next->next = NULL; // Imposta il prossimo a NULL
    }
}

struct Giocatore creaGiocatore(int cl_sd) {
    char nickname[NICKNAME_MAX_LENGTH];
    int nicknameValido = 0;
    memset(nickname, 0, sizeof(nickname)); // Inizializza il nickname
    // Ricevi il nickname dal client
    while (!nicknameValido) {
        // Ricevi il nickname dal client
        if (recv(cl_sd, nickname, sizeof(nickname), 0) <= 0) {
            perror("Errore nella ricezione del nickname");
            close(cl_sd);
            pthread_exit(NULL); // Termina il thread
        }
        // Controlla se il nickname è valido
        if (strlen(nickname) == 0 || strlen(nickname) >= NICKNAME_MAX_LENGTH || nicknameGiaRegistrato(nickname)) {
            printf("Nickname errato o già registrato: %s\n", nickname);
            // Invia un messaggio di errore al client, così può riprovare a inviare un nickname valido
            send(cl_sd, NICKNAME_ERRATO, strlen(NICKNAME_ERRATO), 0);
            memset(nickname, 0, sizeof(nickname)); // Resetta il nickname
        }
        else {
            nicknameValido = 1; // Il nickname è valido
            // Invia un messaggio di conferma al client
            send(cl_sd, NICKNAME_VALIDO, strlen(NICKNAME_VALIDO), 0);
        }
    }
    // Se siamo qui, il nickname è valido, quindi creiamo un nuovo Giocatore
    struct Giocatore nuovoGiocatore;
    nuovoGiocatore.socket = cl_sd;
    memset(nuovoGiocatore.nickname, 0, sizeof(nuovoGiocatore.nickname)); // Pulizia del campo nickname
    strcpy(nuovoGiocatore.nickname, nickname); // Copia il nickname nel nuovo giocatore
    inserisciGiocatore(nuovoGiocatore); // Aggiungi il nuovo giocatore alla lista dei giocatori
    return nuovoGiocatore;

}

void* gestisciConnessione(void* arg) {
    int cl_sd = *(int*)arg;
    struct Giocatore giocatore = creaGiocatore(cl_sd);
    inviaQuizDisponibili(cl_sd); // Funzione per inviare i quiz disponibili al giocatore
    
    close(cl_sd);
    pthread_exit(NULL); // Termina il thread
}

void stampaInterfaccia() {
    printf("Benvenuto nel gioco!\n");
    printf("Server in ascolto su %s:%d\n", SERVER_IP, PORTA);
    printf("Attendi che i giocatori si connettano...\n");
}

int main () {
    int ret, sd, cl_sd, len;
    pthread_t thread_id;
    struct sockaddr_in my_addr, cl_addr;
    stampaInterfaccia(); // Funzione per stampare l'interfaccia grafica
    /* Creazione socket */
    sd = socket(AF_INET, SOCK_STREAM, 0);
    /* Creazione indirizzo */
    memset(&my_addr, 0, sizeof(my_addr)); // Pulizia
    my_addr.sin_family = AF_INET ;
    my_addr.sin_port = htons(PORTA);
    inet_pton(AF_INET, SERVER_IP, &my_addr.sin_addr);
    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    ret = listen(sd, BACKLOG_SIZE);
    int len = sizeof(cl_addr);
    while(1) {
        cl_sd = accept(sd, (struct sockaddr*)&cl_addr, &len);
        if (cl_sd < 0) {
            perror("Errore nell'accept");
            exit(EXIT_FAILURE);
        }
        ret = pthread_create(&thread_id, NULL, gestisciConnessione, (void*)&cl_sd);
        if (ret != 0) {
            perror("Errore nella creazione del thread");
            exit(EXIT_FAILURE);
        }
        
    }
}