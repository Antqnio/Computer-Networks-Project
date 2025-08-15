#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include "include/costanti.h"
#include "include/stampa_delimitatore.h"
#include "include/comandi.h"
#include "include/send_recv_all.h"


#define PORTA 4242
#define BACKLOG_SIZE 10
#define LUNGHEZZA_MASSIMA_QUIZ 10
#define NUMERO_DOMANDE 5

const char RISPOSTA_CORRETTA[] = "Risposta corretta";
const char RISPOSTA_ERRATA[] = "Risposta errata";
pthread_mutex_t mutex_albero_giocatori; // Mutex per proteggere l'accesso all'albero dei giocatori
pthread_mutex_t* mutex_punteggi; // Array di mutex per proteggere l'accesso ai punteggi
pthread_mutex_t* mutex_quiz_completati; // Array di mutex per proteggere l'accesso ai quiz completati

struct Giocatore {
    int socket;
    char nickname[NICKNAME_MAX_LENGTH];
    struct Giocatore* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Giocatore* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};

struct Punteggio {
    struct Giocatore* giocatore;
    uint8_t punteggio; // Punteggio del giocatore per il quiz
    struct Punteggio* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Punteggio* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};

struct Giocatore_Quiz {
    struct Giocatore* giocatore;
    struct Giocatore_Quiz* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Giocatore_Quiz* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};


struct Giocatore* albero_giocatori = NULL; // Lista dei giocatori connessi
struct Punteggio** vettore_punteggi = NULL; // Vettore dei punteggi per ogni quiz
struct Giocatore_Quiz** quiz_completati =  NULL; // Vettore dei quiz completati

//const char QUIZ_DISPONIBILI[numero_di_quiz_disponibili][LUNGHEZZA_MASSIMA_QUIZ] = {"Geografia", "Storia", "Sport", "Cinema", "Arte"};
uint32_t numero_di_quiz_disponibili = 0; // Numero di quiz disponibili
char** quiz_disponibili;

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


void inserisci_giocatore_ricorsivo(struct Giocatore* curr, struct Giocatore* nuovo_giocatore) {
    // Funzione ricorsiva per inserire un giocatore nell'albero binario di ricerca
    if (strcmp(nuovo_giocatore->nickname, curr->nickname) < 0) {
        // Se il nuovo giocatore è "minore" del corrente, va a sinistra
        if (curr->left == NULL) {
            curr->left = nuovo_giocatore;
            curr->left->left = NULL;
            curr->left->right = NULL;
        } else {
            inserisci_giocatore_ricorsivo(curr->left, nuovo_giocatore);
        }
    } else {
        // Altrimenti va a destra
        if (curr->right == NULL) {
            curr->right = nuovo_giocatore;
            curr->right->left = NULL;
            curr->right->right = NULL;
        } else {
            inserisci_giocatore_ricorsivo(curr->right, nuovo_giocatore);
        }
    }
}

void inserisci_giocatore(struct Giocatore* nuovoGiocatore, struct Giocatore** albero) {
    pthread_mutex_lock(&mutex_albero_giocatori); // Blocca l'accesso all'albero dei giocatori
    // Funzione per aggiungere un giocatore alla lista dei giocatori
    if (*albero == NULL) {
        // Se l'albero è vuoto, inizializza l'albero con il nuovo giocatore
        *albero = nuovoGiocatore;
    } else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine alfabetico di nickname
        inserisci_giocatore_ricorsivo(albero_giocatori, nuovoGiocatore);
    }
    pthread_mutex_unlock(&mutex_albero_giocatori); // Sblocca l'accesso all'albero dei giocatori
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

struct Giocatore* crea_giocatore(int cl_sd) {
    char nickname[NICKNAME_MAX_LENGTH];
    uint32_t stato_del_nickname = 0;
    // Ricevi il nickname dal client
    while (stato_del_nickname == NICKNAME_ERRATO) {
        memset(nickname, 0, sizeof(nickname)); // Inizializza il nickname
        // Ricevi la lunghezza del nickname
        uint32_t lunghezza_nickname = 0;
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
    struct Giocatore* nuovoGiocatore = malloc(sizeof(struct Giocatore));
    nuovoGiocatore->socket = cl_sd;
    memset(nuovoGiocatore->nickname, 0, sizeof(nuovoGiocatore->nickname)); // Pulizia del campo nickname
    strcpy(nuovoGiocatore->nickname, nickname); // Copia il nickname nel nuovo giocatore
    nuovoGiocatore->left  = NULL;
    nuovoGiocatore->right = NULL;
    inserisci_giocatore(nuovoGiocatore, &albero_giocatori); // Aggiungi il nuovo giocatore alla lista dei giocatori
    return nuovoGiocatore;

}

void invia_quiz_disponibili(struct Giocatore* giocatore) {
    char messaggio_di_errore[256];
    // Prima invio il numero di quiz disponibili
    uint32_t numero_di_quiz_disponibili_net = htonl(numero_di_quiz_disponibili); // Converto in formato di rete
    size_t dimensione_messaggio = sizeof(numero_di_quiz_disponibili);
    // Scrivo il messaggio di errore
    memset(messaggio_di_errore, 0, sizeof(messaggio_di_errore));
    snprintf(messaggio_di_errore, sizeof(messaggio_di_errore), "Errore nell'invio del numero dei quiz al client %s: ", giocatore->nickname);
    send_all(giocatore->socket, (void*)&numero_di_quiz_disponibili_net, dimensione_messaggio, gestisci_ritorno_recv_send_lato_server, messaggio_di_errore);

    // Ora invio i temi come una stringa unica il cui separatore è '\n'.
    size_t dim = 1024;
    char buffer[dim];
    memset(buffer, 0, dim);
    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        strcat(buffer, quiz_disponibili[i]);
        strcat(buffer, "\n"); // Uso "\n" come separatore
    }
    // Prima di inviare i temi, calcolo la lunghezza del buffer e la converto in formato di rete,
    // per poi inviarla al client.
    send_all(giocatore->socket, (void*)&dim, sizeof(dim), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della lunghezza del buffer dei temi al client");

    // Ora invio il buffer contenente i temi
    send_all(giocatore->socket, (void*)buffer, dim, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio dei temi al client");

}

uint8_t ricevi_quiz_scelto(struct Giocatore* giocatore) {
    ssize_t ret;
    uint8_t quiz_scelto = 0;
    while(quiz_scelto < 1 || quiz_scelto > numero_di_quiz_disponibili) {
        // Ricevo il quiz scelto dal client (uso recv perhé è un uint8_t)
        ret = recv(giocatore->socket, (void*)&quiz_scelto, sizeof(quiz_scelto), 0);
        gestisci_ritorno_recv_send_lato_server(ret, giocatore->socket, "Errore nella ricezione del quiz scelto dal client");
        if (quiz_scelto < 1 || quiz_scelto > numero_di_quiz_disponibili) {
            invia_ack(giocatore->socket, 0); // Invia un ACK negativo al client
        }
        else {
            invia_ack(giocatore->socket, 1); // Invia un ACK positivo al client
            printf("Il client %s ha scelto il quiz: %s\n", giocatore->nickname, quiz_disponibili[quiz_scelto - 1]);
        }
    }
    return quiz_scelto; // Ritorno il numero del quiz scelto
}

void string_to_lower(char *s) {
    // Funzione per convertire una stringa in minuscolo
    for (int i = 0; s[i] != '\0'; i++) {
        s[i] = tolower((unsigned char)s[i]);
    }
}


void rimuovi_giocatore_da_albero_ricorsivo(struct Giocatore** albero, struct Giocatore* giocatore) {
    // Funzione per rimuovere un giocatore dall'albero binario di ricerca
    if (*albero == NULL) {
        return; // Se l'albero è vuoto, ritorna
    }
    if (strcmp(giocatore->nickname, (*albero)->nickname) < 0) {
        // Se il giocatore da rimuovere è "minore", cerca nel sottoalbero sinistro
        rimuovi_giocatore_da_albero_ricorsivo(&(*albero)->left, giocatore);
    } else if (strcmp(giocatore->nickname, (*albero)->nickname) > 0) {
        // Altrimenti cerca nel sottoalbero destro
        rimuovi_giocatore_da_albero_ricorsivo(&(*albero)->right, giocatore);
    } else {
        // Trovato il giocatore da rimuovere
        struct Giocatore* temp = *albero;
        if ((*albero)->left == NULL) {
            *albero = (*albero)->right; // Sostituisci con il sottoalbero destro
            free(temp); // Libera la memoria del nodo rimosso
        } else if ((*albero)->right == NULL) {
            *albero = (*albero)->left; // Sostituisci con il sottoalbero sinistro
            free(temp); // Libera la memoria del nodo rimosso
        } else {
            // Caso in cui ha entrambi i figli: trovo il minimo del sottoalbero destro
            struct Giocatore* min = (*albero)->right;
            while (min->left != NULL) {
                min = min->left;
            }
            strcpy((*albero)->nickname, min->nickname); // Copia il nickname del minimo
            (*albero)->socket = min->socket; // Copia il socket del minimo
            rimuovi_giocatore_da_albero_ricorsivo(&(*albero)->right, min); // Rimuovi il minimo dal sottoalbero destro
            // La free viene fatta nella chiamata ricorsiva
        }
    }
}

void rimuovi_giocatore_da_albero(struct Giocatore** albero, struct Giocatore* giocatore) {
    pthread_mutex_lock(&mutex_albero_giocatori); // Blocca l'accesso all'albero dei giocatori
    rimuovi_giocatore_da_albero_ricorsivo(albero, giocatore);
    pthread_mutex_unlock(&mutex_albero_giocatori); // Sblocca l'accesso all'albero dei giocatori
}

void rimuovi_giocatore_da_punteggi_ricorsivo(struct Punteggio** punteggi, struct Giocatore* giocatore) {
    // Funzione per rimuovere un giocatore dalla lista dei punteggi
    if (*punteggi == NULL) {
        return; // Se la lista è vuota, ritorna
    }
    if (strcmp(giocatore->nickname, (*punteggi)->giocatore->nickname) < 0) {
        // Se il giocatore da rimuovere è "minore", cerca nel sottoalbero sinistro
        rimuovi_giocatore_da_punteggi_ricorsivo(&(*punteggi)->left, giocatore);
    } else if (strcmp(giocatore->nickname, (*punteggi)->giocatore->nickname) > 0) {
        // Altrimenti cerca nel sottoalbero destro
        rimuovi_giocatore_da_punteggi_ricorsivo(&(*punteggi)->right, giocatore);
    } else {
        // Trovato il giocatore da rimuovere
        struct Punteggio* temp = *punteggi;
        if ((*punteggi)->left == NULL) {
            *punteggi = (*punteggi)->right; // Sostituisci con il sottoalbero destro
            free(temp); // Libera la memoria del nodo rimosso
        } else if ((*punteggi)->right == NULL) {
            *punteggi = (*punteggi)->left; // Sostituisci con il sottoalbero sinistro
            // La free viene fatta in elimina_giocatore()
            free(temp); // Libera la memoria del nodo rimosso
        } else {
            // Caso in cui ha entrambi i figli: trovo il minimo del sottoalbero destro
            struct Punteggio* min = (*punteggi)->right;
            while (min->left != NULL) {
                min = min->left;
            }
            (*punteggi)->giocatore = min->giocatore; // Copia il giocatore del minimo
            rimuovi_giocatore_da_punteggi_ricorsivo(&(*punteggi)->right, min->giocatore); // Rimuovi il minimo dal sottoalbero destro
            // La free viene fatta nella chiamata ricorsiva
        }
    }
}


void rimuovi_giocatore_da_punteggi(struct Punteggio** punteggi, struct Giocatore* giocatore, pthread_mutex_t* mutex) {
    pthread_mutex_lock(mutex); // Blocca l'accesso alla lista dei punteggi
    rimuovi_giocatore_da_punteggi_ricorsivo(punteggi, giocatore);
    pthread_mutex_unlock(mutex); // Sblocca l'accesso alla lista dei punteggi
}


void rimuovi_giocatore_da_quiz_completati_ricorsivo(struct Giocatore_Quiz** quiz_completati, struct Giocatore* giocatore) {
    // Funzione per rimuovere un giocatore dalla lista dei quiz completati
    if (*quiz_completati == NULL) {
        return; // Se la lista è vuota, ritorna
    }
    if (strcmp(giocatore->nickname, (*quiz_completati)->giocatore->nickname) < 0) {
        // Se il giocatore da rimuovere è "minore", cerca nel sottoalbero sinistro
        rimuovi_giocatore_da_quiz_completati_ricorsivo(&(*quiz_completati)->left, giocatore);
    } else if (strcmp(giocatore->nickname, (*quiz_completati)->giocatore->nickname) > 0) {
        // Altrimenti cerca nel sottoalbero destro
        rimuovi_giocatore_da_quiz_completati_ricorsivo(&(*quiz_completati)->right, giocatore);
    } else {
        // Trovato il giocatore da rimuovere
        struct Giocatore_Quiz* temp = *quiz_completati;
        if ((*quiz_completati)->left == NULL) {
            *quiz_completati = (*quiz_completati)->right; // Sostituisci con il sottoalbero destro
            free(temp); // Libera la memoria del nodo rimosso
        } else if ((*quiz_completati)->right == NULL) {
            *quiz_completati = (*quiz_completati)->left; // Sostituisci con il sottoalbero sinistro
            free(temp); // Libera la memoria del nodo rimosso
        } else {
            // Caso in cui ha entrambi i figli: trova il minimo del sottoalbero destro
            struct Giocatore_Quiz* min = (*quiz_completati)->right;
            while (min->left != NULL) {
                min = min->left;
            }
            (*quiz_completati)->giocatore = min->giocatore;
            rimuovi_giocatore_da_quiz_completati_ricorsivo(&(*quiz_completati)->right, min->giocatore); // Rimuovi il minimo dal sottoalbero destro
            // La free del Giocatore_Quiz viene fatta nella chiamata ricorsiva
        }
    }
}


void rimuovi_giocatore_da_quiz_completati(struct Giocatore_Quiz** quiz_completati, struct Giocatore* giocatore, pthread_mutex_t* mutex) {
    pthread_mutex_lock(mutex); // Blocca l'accesso alla lista dei quiz completati
    rimuovi_giocatore_da_quiz_completati_ricorsivo(quiz_completati, giocatore);
    pthread_mutex_unlock(mutex); // Sblocca l'accesso alla lista dei quiz completati
}



void elimina_giocatore(struct Giocatore* curr) {
    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        rimuovi_giocatore_da_punteggi(&vettore_punteggi[i], curr, &mutex_punteggi[i]);
        rimuovi_giocatore_da_quiz_completati(&quiz_completati[i], curr, &mutex_quiz_completati[i]);
    }
    rimuovi_giocatore_da_albero(&albero_giocatori, curr);
}




void inserisci_giocatore_quiz_completato_ricorsivo(struct Giocatore* curr, struct Giocatore_Quiz** quiz_completato) {
    // Funzione per inserire un giocatore nell'albero dei quiz completati
    if (*quiz_completato == NULL) {
        // Se l'albero è vuoto, inizializza l'albero con il nuovo giocatore
        *quiz_completato = malloc(sizeof(struct Giocatore_Quiz));
        (*quiz_completato)->giocatore = curr;
        (*quiz_completato)->left = NULL;
        (*quiz_completato)->right = NULL;
    } else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine alfabetico di nickname
        if (strcmp(curr->nickname, (*quiz_completato)->giocatore->nickname) < 0) {
            inserisci_giocatore_quiz_completato_ricorsivo(curr, &(*quiz_completato)->left);
        } else {
            inserisci_giocatore_quiz_completato_ricorsivo(curr, &(*quiz_completato)->right);
        }
    }

}

void inserisci_giocatore_quiz_completato(struct Giocatore* curr, struct Giocatore_Quiz** quiz_completato, pthread_mutex_t* mutex) {
    pthread_mutex_lock(mutex); // Blocca l'accesso all'albero dei quiz completati
    inserisci_giocatore_quiz_completato_ricorsivo(curr, quiz_completato);
    pthread_mutex_unlock(mutex); // Sblocca l'accesso all'albero dei quiz
}


struct Punteggio* crea_punteggio(struct Giocatore* curr, uint32_t curr_punteggio) {
    // Funzione per creare un nuovo punteggio
    struct Punteggio* nuovoPunteggio = malloc(sizeof(struct Punteggio));
    nuovoPunteggio->giocatore = curr;
    nuovoPunteggio->punteggio = curr_punteggio; // Inizializzo il punteggio a 1
    nuovoPunteggio->left = NULL;
    nuovoPunteggio->right = NULL;
    return nuovoPunteggio;
}

void salva_quiz_completato(struct Giocatore* curr, char quiz_scelto) {
    inserisci_giocatore_quiz_completato(curr, &quiz_completati[quiz_scelto - 1], &mutex_quiz_completati[quiz_scelto - 1]); // Inserisco il giocatore nell'albero dei giocatori
}

void aggiorna_punteggio_ricorsivo(struct Giocatore* curr, uint8_t quiz_scelto, uint32_t nuovo_punteggio) {
    // Funzione per cambiare il punteggio del giocatore curr nel quiz scelto.
    // L'albero è ordinato in ordine di punteggio e, a parità di punteggio, in ordine alfabetico di nickname.

    if (vettore_punteggi[quiz_scelto - 1] == NULL) {
        // Se l'albero dei punteggi è vuoto, inizializza l'albero con il nuovo giocatore
        struct Punteggio* nuovoPunteggio = crea_punteggio(curr, 1);
        vettore_punteggi[quiz_scelto - 1] = nuovoPunteggio;
    }
    else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine di punteggio
        // e, a parità di punteggio, in ordine alfabetico di nickname.
        struct Punteggio* curr_p = vettore_punteggi[quiz_scelto - 1];
        while (1) {
            if (strcmp(curr->nickname, curr_p->giocatore->nickname) == 0) {
                // Se il giocatore è già presente, incrementa il punteggio.
                // Per farlo, cancello il nodo corrente e creo un nuovo nodo con il punteggio incrementato.
                // Devo fare così, altrimenti romperei l'ordinamento.
                rimuovi_giocatore_da_punteggi(&vettore_punteggi[quiz_scelto - 1], curr, &mutex_punteggi[quiz_scelto - 1]);
                aggiorna_punteggio_ricorsivo(curr, quiz_scelto, nuovo_punteggio);
                return;
            }
            else {
                if (curr_p->punteggio < nuovo_punteggio) {
                    // Se il punteggio del giocatore corrente è minore del nuovo punteggio,
                    // inserisco il nuovo giocatore prima di curr_p.
                    struct Punteggio* nuovoPunteggio = crea_punteggio(curr, nuovo_punteggio);
                    if (curr_p->left == NULL) {
                        curr_p->left = nuovoPunteggio; // Inserisco a sinistra
                        return;
                    } else {
                        curr_p->right = nuovoPunteggio; // Inserisco a destra
                        return;
                    }
                } else if (curr_p->punteggio > nuovo_punteggio) {
                    // Altrimenti, continuo a cercare nel sottoalbero destro
                    if (curr_p->right == NULL) {
                        curr_p->right = crea_punteggio(curr, nuovo_punteggio);
                        return;
                    }
                    curr_p = curr_p->right; // Scendo nel sottoalbero destro
                }
                else {
                    // Se il punteggio è uguale, inserisco in ordine alfabetico di nickname
                    if (strcmp(curr->nickname, curr_p->giocatore->nickname) < 0) {
                        // Se il nickname del giocatore corrente è "minore", inserisco a sinistra
                        if (curr_p->left == NULL) {
                            curr_p->left = crea_punteggio(curr, nuovo_punteggio);
                            return;
                        } else {
                            curr_p = curr_p->left; // Scendo nel sottoalbero sinistro
                        }
                    } else {
                        // Altrimenti, inserisco a destra
                        if (curr_p->right == NULL) {
                            curr_p->right = crea_punteggio(curr, nuovo_punteggio);
                            return;
                        } else {
                            curr_p = curr_p->right; // Scendo nel sottoalbero destro
                        }
                    }
                }
            }
        }
    }
}


void aggiorna_punteggio(struct Giocatore* curr, uint8_t quiz_scelto, uint32_t nuovo_punteggio) {
    pthread_mutex_lock(&mutex_punteggi[quiz_scelto - 1]); // Blocca l'accesso all'albero dei punteggi
    aggiorna_punteggio_ricorsivo(curr, quiz_scelto, nuovo_punteggio);
    pthread_mutex_unlock(&mutex_punteggi[quiz_scelto - 1]); // Sblocca l'accesso all'albero dei punteggi
}


void stampa_punteggi(struct Punteggio* curr) {
    // Funzione per stampare i punteggi in ordine di punteggio e, a parità di punteggio, in ordine alfabetico di nickname
    if (curr == NULL) {
        return; // Se l'albero è vuoto, ritorna
    }
    stampa_punteggi(curr->left); // Stampa il sottoalbero sinistro
    printf("- %s %d\n", curr->giocatore->nickname, curr->punteggio);
    stampa_punteggi(curr->right); // Stampa il sottoalbero destro
}

void stampa_giocatori_quiz_completati(struct Giocatore_Quiz* curr) {
    // Funzione per stampare i giocatori che hanno completato il quiz in ordine alfabetico di nickname
    if (curr == NULL) {
        return; // Se l'albero è vuoto, ritorna
    }
    stampa_giocatori_quiz_completati(curr->left); // Stampa il sottoalbero sinistro
    printf("- %s\n", curr->giocatore->nickname);
    stampa_giocatori_quiz_completati(curr->right); // Stampa il sottoalbero destro
}


void stampa_interfaccia() {
    stampa_delimitatore();
    printf("Temi:\n");
    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        printf("%d - %s\n", i + 1, quiz_disponibili[i]);
    }
    stampa_delimitatore();
    printf("\n");

    printf("Partecipanti (%d):\n", ottieni_partecipanti(albero_giocatori));
    stampa_partecipanti(albero_giocatori); // Funzione per stampare i partecipanti
    printf("\n\n");

    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        if (vettore_punteggi[i] == NULL) {
            continue; // Se non ci sono punteggi per questo tema, salta
        }
        printf("Punteggi tema %d:\n", i + 1);
        stampa_punteggi(vettore_punteggi[i]); // Funzione per stampare i punteggi
        printf("\n");
    }
    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        if (quiz_completati[i] == NULL) {
            continue; // Se non ci sono quiz completati per questo tema, salta
        }
        printf("Quiz tema %d completato\n", i + 1);
        stampa_giocatori_quiz_completati(quiz_completati[i]); // Funzione per stampare i giocatori che hanno completato il quiz
        printf("\n");
    }
    printf("------\n");
}

void crea_stringa_punteggio(int sd, struct Punteggio* punteggio, char* buffer, uint32_t buf_size, char* start) {
    // Funzione per inviare il punteggio al client
    // Invia il punteggio del giocatore al client in ordine di punteggio e, a parità di punteggio, in ordine alfabetico di nickname
    if (punteggio == NULL) {
        return; // Se il punteggio è NULL, non invio nulla
    }
    uint32_t punteggio_net = htonl(punteggio->punteggio); // Converto il punteggio in formato di rete
    crea_stringa_punteggio(sd, punteggio->left, buffer, buf_size, start); // Invia il punteggio del sottoalbero sinistro

    // Salva il punteggio
    snprintf(buffer + *start, buf_size, "%d|",  punteggio->punteggio);
    // Salva il nickname del giocatore associato a tale punteggio
    snprintf(buffer + *start + strlen(buffer + *start), buf_size - *start, "%s\n", punteggio->giocatore->nickname);
    *start += strlen(buffer + *start); // Aggiorna l'indice di partenza per il prossimo punteggio


    send_all(sd, (void*)&punteggio_net, sizeof(punteggio_net), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio del punteggio al client");
    // Invia il nickname del giocatore
    send_all(sd, (void*)punteggio->giocatore->nickname, strlen(punteggio->giocatore->nickname) + 1, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio del nickname del giocatore al client");
    crea_stringa_punteggio(sd, punteggio->right, buffer, buf_size, start); // Invia il punteggio del sottoalbero destro
}

void invia_punteggio_di_ogni_tema(int sd) {
    // Funzione per inviare il punteggio di ogni tema al client
    // Per ogni tema, invia una stringa col seguente formato:
    // "<numero_tema>\n<punteggio>|<nickname>\n"
    // Per capire se ho un numero_tema o un punteggio con nickname, basta vedere se nella riga ho un '|'
    char buffer[1024];
    char start = 0;
    memset(buffer, 0, sizeof(buffer)); // Inizializza il buffer
    for (uint8_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        if (vettore_punteggi[i] == NULL) {
            continue; // Se non ci sono punteggi per questo tema, salta
        }
        snprintf(buffer + start, sizeof(buffer) - start, "%d\n", i + 1); // Aggiungo il numero del tema
        start += strlen(buffer + start); // Aggiorno l'indice di partenza
        crea_stringa_punteggio(sd, vettore_punteggi[i], buffer, sizeof(buffer), &start); // Invia i punteggi del tema al client
        // Invio la lunghezza della stringa al client
        uint32_t lunghezza_buffer = htonl(strlen(buffer)); // Converto la lunghezza in formato di rete
        send_all(sd, (void*)&lunghezza_buffer, sizeof(lunghezza_buffer), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della lunghezza del buffer dei punteggi al client");
        // Invio la stringa al client                  
        send_all(sd, (void*)buffer, strlen(buffer), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio dei punteggi del tema al client");
    }
}

void invia_risposta(int sd, const char* risposta, uint8_t dimensione_risposta) {
    // Funzione per inviare la risposta del server al client
    // Il client sa che la dimensione della risposta è di 18 byte (compresi i \0)
    send(sd, &dimensione_risposta, sizeof(dimensione_risposta), 0);
    // Invia la risposta al client
    send_all(sd, (void*)risposta, dimensione_risposta, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della risposta del server al client");
    
}

void invia_risposta_corretta(int sd) {
    const char risposta_corretta[DIMENSIONE_RISPOSTA] = "Risposta corretta";
    invia_risposta(sd, risposta_corretta, DIMENSIONE_RISPOSTA);
}

void invia_risposta_errata(int sd) {
    char risposta_errata[DIMENSIONE_RISPOSTA];
    memset(risposta_errata, 0, sizeof(risposta_errata));
    strcpy(risposta_errata, "Risposta errata");
    invia_risposta(sd, risposta_errata, DIMENSIONE_RISPOSTA);
}


void* gestisci_connessione(void* arg) {
    int cl_sd = *(int*)arg;
    int ret;
    uint8_t quiz_scelto;
    char domanda[1024], risposta_client[1024], risposta_server[1024]; // Buffer per le domande
    uint8_t dimensione_domanda;
    uint8_t dimensione_risposta;
    uint8_t domanda_non_ancora_inviata = 0; // Variabile per non inviare più volte la stessa domanda al client (quando il client invia "show score" o "endquiz")
    uint32_t punteggio; // Punteggio del giocatore per il quiz
    struct Giocatore* giocatore = crea_giocatore(cl_sd);
    stampa_interfaccia(); // Stampa l'interfaccia grafica del server
    // A questo punto, finché il client non interrompe la connessione (a meno di errori):
    // 1. Invia i quiz disponibili al client
    // 2. Riceve il quiz scelto dal client
    // 3. Inizia il quiz con il client
    while(1) {
        int c = 0;
        invia_quiz_disponibili(giocatore); // Funzione per inviare i quiz disponibili al giocatore
        quiz_scelto = ricevi_quiz_scelto(giocatore); // Funzione per ricevere il quiz scelto dal giocatore
        // Adesso il client può iniziare a giocare con il quiz scelto.
        int domande_inviate = 0; // Contatore delle domande inviate
        char filename[256];
        memset(filename, 0, sizeof(filename)); // Inizializzo il buffer del nome del file
        snprintf(filename, sizeof(filename), "quiz/%d.txt", quiz_scelto); // Costruisco il nome del file del quiz scelto
        FILE *fp = fopen(filename, "r");
        if (fp == NULL) {
            perror("Errore apertura file");
            close(cl_sd);
            pthread_exit(NULL); // Termina il thread
        }
        domanda_non_ancora_inviata = 1; // Setto la variabile per indicare che la domanda non è ancora stata inviata
        // Leggo le domande dal file e le invio al client
        punteggio = 0; // Resetto il punteggio per il nuovo quiz
        while (c != EOF && domande_inviate < NUMERO_DOMANDE) {
            // Invio, in ordine di scrittura, le domande del quiz scelto al client
            int i = 0;
            memset(domanda, 0, sizeof(domanda)); // Inizializzo il buffer della domanda
            // risposta_ricevuta serve per non cosniderare "endquiz" e "show score" come risposte valide
            if (domanda_non_ancora_inviata) {
                while ((c = fgetc(fp)) != EOF) { // Leggo il file fino alla fine
                    if (c == '?') {
                        domanda[i++] = c; // Aggiungo il carattere '?' al buffer della domanda
                        break;  // Trovato il carattere '?', interrompe
                    }
                    domanda[i++] = c; // Aggiungo il carattere al buffer della domanda
                }
                // Invio la lunghezza della domanda al client
                dimensione_domanda = i; // La dimensione della domanda è pari al numero di caratteri letti
                send(cl_sd, &dimensione_domanda, sizeof(dimensione_domanda), 0); // Invia la dimensione della domanda al client
                gestisci_ritorno_recv_send_lato_server(dimensione_domanda, cl_sd, "Errore nell'invio della dimensione della domanda al client");
                // Invio la domanda al client
                printf("Invio domanda al client %s: %s\n", giocatore->nickname, domanda);
                send_all(cl_sd, (void*)domanda, i, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della domanda al client");
            }
            // Ricevo la dimensione della risposta dal client (tale dimensione è di 1 byte)
            ret = recv(cl_sd, &dimensione_risposta, sizeof(dimensione_risposta), 0);
            gestisci_ritorno_recv_send_lato_server(ret, cl_sd, "Errore nella ricezione della dimensione della risposta dal client");
            // Ricevo la risposta del client
            memset(risposta_client, 0, sizeof(risposta_client)); // Inizializzo il buffer della risposta
            recv_all(cl_sd, (void*)risposta_client, dimensione_risposta, gestisci_ritorno_recv_send_lato_server, "Errore nella ricezione della risposta dal client");
            string_to_lower(risposta_client); // Converto la risposta del client in minuscolo
            printf("Ricevuta risposta dal client %s: %s\n", giocatore->nickname, risposta_client);
            if (strcmp(risposta_client, "show score") == 0) {
                invia_punteggio_di_ogni_tema(cl_sd);
                domanda_non_ancora_inviata = 0; // Segno che il client ha già ricevuto la domanda
                continue;
            }
            if (strcmp(risposta_client, "endquiz") == 0) {
                invia_ack(cl_sd, 1); // Invia un ACK per terminare il quiz
                elimina_giocatore(giocatore); // Elimina il giocatore dalla lista
                stampa_interfaccia(); // Stampa l'interfaccia aggiornata
                fclose(fp); // Chiudo il file
                close(cl_sd); // Chiudo il socket del client
                pthread_exit(NULL); // Termina il thread
            }
            c = fgetc(fp); // Leggo il file per scartare lo spazio tra la domanda e le risposte
            i = 0; // Resetto l'indice per la risposta
            memset(risposta_server, 0, sizeof(risposta_server)); // Inizializzo il buffer della risposta del server
            while ((c = fgetc(fp)) != EOF) { // Leggo il file fino alla fine (la fine della risposta è segnata da un carattere '\n'. Le varie risposte valide sono separate da '|')
                if (c == '|') { // Ho letto una risposta (valida) dal file
                    if (strcmp(risposta_client, risposta_server) == 0) {
                        // Se trovo il carattere '|' e la risposta del client corrisponde alla risposta del server,
                        // invio un messaggio di successo al client
                        invia_risposta_corretta(cl_sd);
                        printf("Risposta corretta del client %s: %s\n", giocatore->nickname, risposta_client);
                        aggiorna_punteggio(giocatore, quiz_scelto, ++punteggio); // Incrementa il punteggio del client per questo tema
                        break;
                    }
                    memset(risposta_server, 0, sizeof(risposta_server)); // Resetto il buffer della risposta del server, così da fare il test con un'altra risposta valida
                    i = 0; // Resetto l'indice per la risposta del server
                }
                else if (c == '\n') { // Ho letto l'ultima risposta (valida) dal file per quella domanda
                    if (strcmp(risposta_client, risposta_server) == 0) {
                        // Se la risposta del client corrisponde alla risposta del server, aggiorno il punteggio
                        invia_risposta_corretta(cl_sd); // Invia un messaggio di successo al client
                        printf("Risposta corretta del client %s: %s\n", giocatore->nickname, risposta_client);
                        aggiorna_punteggio(giocatore, quiz_scelto, ++punteggio); // Incrementa il punteggio del client
                        stampa_interfaccia(); // Stampa l'interfaccia aggiornata
                    } else {
                        invia_risposta_errata(cl_sd); // Invia un messaggio di errore al client
                        invia_ack(cl_sd, 0); // Invia un ACK negativo al client
                    }
                    break;  // Trovato il carattere '\n', interrompe, così da passare alla prossima domanda
                }
                risposta_server[i++] = c; // Aggiungo il carattere al buffer della risposta
                // printf("Risposta del server: %s\n", risposta_server); // Stampo la risposta del server
            }

            domanda_non_ancora_inviata = 1; // Setto la variabile per la prossima domanda
            ++domande_inviate; // Incremento il contatore delle domande inviate
            while (c != '\n' && c != EOF) {
                // Scarto il resto della riga (fino al carattere '\n' o EOF) per passare alla prossima domanda
                c = fgetc(fp);
            }
        }
        // Questo client ha completato il quiz
        salva_quiz_completato(giocatore, quiz_scelto);
        fclose(fp); // Chiudo il file
    }
    // A regola non dovrei mai arrivare qui, ma per sicurezza chiudo il socket del client e termino il thread
    close(cl_sd); // Chiudo il socket del client
    pthread_exit(NULL); // Termina il thread
}


void ottieni_quiz_disponibili() {
    // Legge, da info.txt, i temi dei quiz disponibili e li memorizza in un array di stringhe
    FILE *fp = fopen("quiz/info.txt", "r");
    if (!fp) {
        perror("Errore apertura file");
        return;
    }

    int numero_temi;
    if (fscanf(fp, "%d\n", &numero_temi) != 1) {
        fprintf(stderr, "Errore lettura numero temi\n");
        fclose(fp);
        return;
    }

    // Alloca array di puntatori a stringa dove andrò a scrivere ogni tema letto dal file quiz_scelto.txt
    char **temi = malloc(numero_temi * sizeof(char *));
    if (!temi) {
        perror("Errore nella malloc");
        fclose(fp);
        return;
    }

    char buffer[256]; // buffer temporaneo per leggere le righe
    memset(buffer, 0, sizeof(buffer)); // Inizializzo il buffer
    for (int i = 0; i < numero_temi; i++) {
        if (!fgets(buffer, sizeof(buffer), fp)) {
            fprintf(stderr, "Errore lettura riga %d\n", i + 1);
            // Libero ciò che ho allocato finora
            for (int j = 0; j < i; j++) {
                free(temi[j]);
            }
            free(temi);
            fclose(fp);
            return;
        }

        // Rimuovo, se presente, il newline finale (e ci metto il terminatore di stringa)
        // Uso strcspn() per trovare la posizione del primo newline e lo sostituisco con il terminatore di stringa
        buffer[strcspn(buffer, "\n")] = '\0';

        // Alloca la memoria per la stringa che rappresenta il tema corrente
        temi[i] = malloc(strlen(buffer) + 1);
        if (!temi[i]) {
            perror("Errore malloc");
            // Come prima, in caso di errore, dealloco tutto
            for (int j = 0; j < i; j++) {
                free(temi[j]);
            }
            free(temi);
            fclose(fp);
            return;
        }
        strcpy(temi[i], buffer);
    }
    // Assegno l'array di temi al puntatore passato come argomento
    quiz_disponibili = temi;
    numero_di_quiz_disponibili = numero_temi; // Aggiorno il numero di quiz disponibili
    vettore_punteggi = malloc(numero_di_quiz_disponibili * sizeof(struct Punteggio*)); // Alloco il vettore dei punteggi
    quiz_completati = malloc(numero_di_quiz_disponibili * sizeof(struct Giocatore_Quiz*)); // Alloco il vettore dei quiz completati
    if (!vettore_punteggi || !quiz_completati) {
        perror("Errore durante la malloc o del vettore punteggi o del vettore quiz completati");
        // Dealloco i temi e chiudo il file
        for (int i = 0; i < numero_temi; i++) {
            free(temi[i]);
        }
        free(temi);
        fclose(fp);
        return;
    }
    mutex_punteggi = malloc(numero_di_quiz_disponibili * sizeof(pthread_mutex_t)); // Alloco il vettore dei mutex per i punteggi
    mutex_quiz_completati = malloc(numero_di_quiz_disponibili * sizeof(pthread_mutex_t)); // Alloco il vettore dei mutex per i quiz completati
    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        vettore_punteggi[i] = NULL; // Inizializzo il vettore dei punteggi a NULL
        quiz_completati[i] = NULL; // Inizializzo il vettore dei quiz completati a NULL
        pthread_mutex_init(&mutex_punteggi[i], NULL); // Inizializzo il mutex per i punteggi
        pthread_mutex_init(&mutex_quiz_completati[i], NULL); // Inizializzo il mutex per i quiz completati
    }
    // Chiudo il file
    fclose(fp);
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
    pthread_mutex_init(&mutex_albero_giocatori, NULL); // Inizializzo il mutex per la mutual esclusione dell'albero dei giocatori
    ottieni_quiz_disponibili(); // Funzione per ottenere i quiz disponibili
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