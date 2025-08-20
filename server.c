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

const char RISPOSTA_CORRETTA[] = "Risposta corretta";
const char RISPOSTA_ERRATA[] = "Risposta errata";
pthread_mutex_t mutex_albero_giocatori; // Mutex per proteggere l'accesso all'albero dei giocatori
pthread_mutex_t* mutex_punteggi; // Array di mutex per proteggere l'accesso ai punteggi
pthread_mutex_t* mutex_quiz_completati; // Array di mutex per proteggere l'accesso ai quiz completati

struct Giocatore {
    int socket;
    char nickname[NICKNAME_MAX_LENGTH];
    uint8_t* punteggio; // Puntatore al vettore dei punteggi del giocatore
    struct Giocatore* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Giocatore* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};

struct Punteggio {
    struct Giocatore* giocatore;
    // Il punteggio è salvato dentro giocatore->punteggio
    // Uso questa struttura per stampa ordinata in O(n)
    struct Punteggio* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Punteggio* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};

struct Quiz_Completato {
    struct Giocatore* giocatore;
    struct Quiz_Completato* left; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
    struct Quiz_Completato* right; // Puntatore al prossimo giocatore nell'albero binario di ricerca.
};


struct Giocatore* albero_giocatori = NULL; // Lista dei giocatori connessi
struct Punteggio** vettore_punteggi = NULL; // Vettore dei punteggi per ogni quiz
struct Quiz_Completato** quiz_completati =  NULL; // Vettore contenente gli alberi dei quiz completati

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

void inserisci_giocatore(struct Giocatore* nuovo_giocatore, struct Giocatore** albero) {
    pthread_mutex_lock(&mutex_albero_giocatori); // Blocca l'accesso all'albero dei giocatori
    // Funzione per aggiungere un giocatore alla lista dei giocatori
    if (*albero == NULL) {
        // Se l'albero è vuoto, inizializza l'albero con il nuovo giocatore
        *albero = nuovo_giocatore;
    } else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine alfabetico di nickname
        inserisci_giocatore_ricorsivo(albero_giocatori, nuovo_giocatore);
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
            free(temp->punteggio); // Libera la memoria del vettore dei punteggi
            free(temp); // Libera la memoria del nodo rimosso
        } else if ((*albero)->right == NULL) {
            *albero = (*albero)->left; // Sostituisci con il sottoalbero sinistro
            free(temp->punteggio); // Libera la memoria del vettore dei punteggi
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
            // La free viene fatta nei casi base
        }
    }
}

void rimuovi_giocatore_da_albero(struct Giocatore** albero, struct Giocatore* giocatore) {
    pthread_mutex_lock(&mutex_albero_giocatori); // Blocca l'accesso all'albero dei giocatori
    rimuovi_giocatore_da_albero_ricorsivo(albero, giocatore);
    pthread_mutex_unlock(&mutex_albero_giocatori); // Sblocca l'accesso all'albero dei giocatori
}

void rimuovi_giocatore_da_punteggi_ricorsivo(struct Punteggio** curr, struct Giocatore* giocatore, uint8_t quiz_scelto) {
    // Funzione per rimuovere un giocatore dall'albero dei punteggi
    // Si ricorda che l'albero dei punteggi è un albero binario di ricerca ordinato per punteggio e,
    // a parità di punteggio, a sinistra ho i nickname "maggiori" e a destra i nickname "minori" (questo
    // permette di avere un ordinamento alfabetico dei nickname a parità di punteggio nella visita post-order)
    if (*curr == NULL) {
        return; // Se la lista è vuota, ritorna
    }
    if (giocatore->punteggio[quiz_scelto - 1] < (*curr)->giocatore->punteggio[quiz_scelto - 1]) {
        // Se il giocatore da rimuovere è "minore", cerca nel sottoalbero sinistro
        rimuovi_giocatore_da_punteggi_ricorsivo(&(*curr)->left, giocatore, quiz_scelto);
    } else if (giocatore->punteggio[quiz_scelto - 1] > (*curr)->giocatore->punteggio[quiz_scelto - 1]) {
        // Altrimenti cerca nel sottoalbero destro
        rimuovi_giocatore_da_punteggi_ricorsivo(&(*curr)->right, giocatore, quiz_scelto);
    } else {
        // Caso in cui il punteggio è uguale: devo verificare il nickname
        if (strcmp(giocatore->nickname, (*curr)->giocatore->nickname) < 0) {
            // Se il giocatore da rimuovere è "minore", cerca nel sottoalbero destro
            rimuovi_giocatore_da_punteggi_ricorsivo(&(*curr)->right, giocatore, quiz_scelto);
        }
        else if (strcmp(giocatore->nickname, (*curr)->giocatore->nickname) > 0) {
            // Altrimenti cerca nel sottoalbero sinistro
            rimuovi_giocatore_da_punteggi_ricorsivo(&(*curr)->left, giocatore, quiz_scelto);
        }
        else {
            // Trovato il giocatore da rimuovere
            struct Punteggio* temp = *curr;
            if ((*curr)->left == NULL) {
                *curr = (*curr)->right; // Sostituisci con il sottoalbero destro
                free(temp); // Libera la memoria del nodo rimosso
            } else if ((*curr)->right == NULL) {
                *curr = (*curr)->left; // Sostituisci con il sottoalbero sinistro
                // La free viene fatta in elimina_giocatore()
                free(temp); // Libera la memoria del nodo rimosso
            } else {
                // Caso in cui ha entrambi i figli: trovo il minimo del sottoalbero destro
                // Il minimo del sottoalbero destro è, per definizione, il successore in-order del nodo da rimuovere
                // Questo vale anche con la logica di ordinamento che ho scelto per l'albero dei punteggi, ovvero
                // che a parità di punteggio, inserisco a destra i nickname "minori" e a sinistra i nickname "maggiori",
                struct Punteggio* min = (*curr)->right;
                while (min->left != NULL) {
                    min = min->left;
                }
                (*curr)->giocatore = min->giocatore; // Copia il giocatore del minimo
                rimuovi_giocatore_da_punteggi_ricorsivo(&(*curr)->right, min->giocatore, quiz_scelto); // Rimuovi il minimo dal sottoalbero destro
                // La free viene fatta nei casi base
            }
        }
    }
}


void rimuovi_giocatore_da_punteggi(struct Punteggio** punteggi, struct Giocatore* giocatore, uint8_t quiz_scelto, pthread_mutex_t* mutex) {
    pthread_mutex_lock(mutex); // Blocca l'accesso alla lista dei punteggi
    rimuovi_giocatore_da_punteggi_ricorsivo(punteggi, giocatore, quiz_scelto);
    pthread_mutex_unlock(mutex); // Sblocca l'accesso alla lista dei punteggi
}


void rimuovi_giocatore_da_quiz_completati_ricorsivo(struct Quiz_Completato** quiz_completati, struct Giocatore* giocatore) {
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
        struct Quiz_Completato* temp = *quiz_completati;
        if ((*quiz_completati)->left == NULL) {
            *quiz_completati = (*quiz_completati)->right; // Sostituisci con il sottoalbero destro
            free(temp); // Libera la memoria del nodo rimosso
        } else if ((*quiz_completati)->right == NULL) {
            *quiz_completati = (*quiz_completati)->left; // Sostituisci con il sottoalbero sinistro
            free(temp); // Libera la memoria del nodo rimosso
        } else {
            // Caso in cui ha entrambi i figli: trova il minimo del sottoalbero destro
            struct Quiz_Completato* min = (*quiz_completati)->right;
            while (min->left != NULL) {
                min = min->left;
            }
            (*quiz_completati)->giocatore = min->giocatore;
            rimuovi_giocatore_da_quiz_completati_ricorsivo(&(*quiz_completati)->right, min->giocatore); // Rimuovi il minimo dal sottoalbero destro
            // La free del Quiz_Completato viene fatta nella chiamata ricorsiva
        }
    }
}


void rimuovi_giocatore_da_quiz_completati(struct Quiz_Completato** quiz_completati, struct Giocatore* giocatore, pthread_mutex_t* mutex) {
    pthread_mutex_lock(mutex); // Blocca l'accesso alla lista dei quiz completati
    rimuovi_giocatore_da_quiz_completati_ricorsivo(quiz_completati, giocatore);
    pthread_mutex_unlock(mutex); // Sblocca l'accesso alla lista dei quiz completati
}



void elimina_giocatore(struct Giocatore* giocatore) {
    for (uint8_t quiz_selezionato = 1; quiz_selezionato <= numero_di_quiz_disponibili; ++quiz_selezionato) {
        rimuovi_giocatore_da_punteggi(&vettore_punteggi[quiz_selezionato - 1], giocatore, quiz_selezionato, &mutex_punteggi[quiz_selezionato - 1]);
        rimuovi_giocatore_da_quiz_completati(&quiz_completati[quiz_selezionato - 1], giocatore, &mutex_quiz_completati[quiz_selezionato - 1]);
    }
    rimuovi_giocatore_da_albero(&albero_giocatori, giocatore);
}


struct Giocatore* trova_giocatore(int sd, struct Giocatore* curr) {
    // Funzione per trovare un giocatore nell'albero dei giocatori (è il curr da passare alla prima chiamata)
    struct Giocatore* giocatore = NULL;
    if (curr == NULL) {
        return NULL; // Se l'albero è vuoto, ritorna NULL
    }
    if (curr->socket == sd) {
        return curr; // Trovato il giocatore con il socket corrispondente
    }
    giocatore = trova_giocatore(sd, curr->left);
    return giocatore ? giocatore : trova_giocatore(sd, curr->right);
}

void stampa_punteggi(struct Punteggio* curr, uint8_t quiz_selezionato) {
    // Funzione per stampare i punteggi in ordine decrescente di punteggio e,
    // a parità di punteggio, in ordine alfabetico di nickname.
    // Per farlo, faccio una visita post-order dell'albero dei punteggi passato come argomneto.
    if (curr == NULL) {
        return; // Se l'albero è vuoto, ritorna
    }
    stampa_punteggi(curr->right, quiz_selezionato); // Stampa il sottoalbero destro
    printf("- %s %d\n", curr->giocatore->nickname, curr->giocatore->punteggio[quiz_selezionato - 1]);
    stampa_punteggi(curr->left, quiz_selezionato); // Stampa il sottoalbero sinistro
}

void stampa_giocatori_quiz_completati(struct Quiz_Completato* curr) {
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
    for (uint8_t quiz_selezionato = 1; quiz_selezionato <= numero_di_quiz_disponibili; ++quiz_selezionato) {
        printf("%d - %s\n", quiz_selezionato, quiz_disponibili[quiz_selezionato - 1]);
    }
    stampa_delimitatore();
    printf("\n");

    printf("Partecipanti (%d):\n", ottieni_partecipanti(albero_giocatori));
    stampa_partecipanti(albero_giocatori); // Funzione per stampare i partecipanti
    printf("\n\n");

    for (uint8_t quiz_selezionato = 1; quiz_selezionato <= numero_di_quiz_disponibili; ++quiz_selezionato) {
        if (vettore_punteggi[quiz_selezionato - 1] == NULL) {
            continue; // Se non ci sono punteggi per questo tema, salta
        }
        printf("Punteggi tema %d:\n", quiz_selezionato);
        stampa_punteggi(vettore_punteggi[quiz_selezionato - 1], quiz_selezionato); // Funzione per stampare i punteggi
        printf("\n");
    }
    for (uint8_t quiz_selezionato = 1; quiz_selezionato <= numero_di_quiz_disponibili; ++quiz_selezionato) {
        if (quiz_completati[quiz_selezionato - 1] == NULL) {
            continue; // Se non ci sono quiz completati per questo tema, salta
        }
        printf("Quiz tema %d completato\n", quiz_selezionato);
        stampa_giocatori_quiz_completati(quiz_completati[quiz_selezionato - 1]); // Funzione per stampare i giocatori che hanno completato il quiz
        printf("\n");
    }
    printf("------\n");
}



void gestisci_ritorno_recv_send_lato_server(int ret, int sd, const char* msg) {
    // Funzione per gestire il ritorno di recv
    if (ret <= 0) {
        if (ret == 0) {
            printf("Socket %d lato client chiuso.\n", sd);
        } else {
            perror(msg);
        }
        // Chiudo il socket associato al client sia nel caso di errore che di chiusura
        // Elimino il giocatore associato al socket
        // Trovare il giocatore è O(n), però ci si aspetta che le disconnessioni non siano tanto frequenti
        struct Giocatore* giocatore = trova_giocatore(sd, albero_giocatori);
        elimina_giocatore(giocatore);
        close(sd);
        stampa_interfaccia(); // Stampa l'interfaccia dopo la disconnessione
        pthread_exit(NULL); // Termina il thread
    }
}

void invia_ack(int sd, uint8_t ack) {
    // Funzione per inviare un ACK al client
    send_all(sd, (void*)&ack, sizeof(ack), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio dell'ACK al client");
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
    struct Giocatore* nuovo_giocatore = malloc(sizeof(struct Giocatore));
    nuovo_giocatore->socket = cl_sd;
    memset(nuovo_giocatore->nickname, 0, sizeof(nuovo_giocatore->nickname)); // Pulizia del campo nickname
    strcpy(nuovo_giocatore->nickname, nickname); // Copia il nickname nel nuovo giocatore
    nuovo_giocatore->left  = NULL;
    nuovo_giocatore->right = NULL;
    nuovo_giocatore->punteggio = calloc(numero_di_quiz_disponibili, sizeof(uint8_t)); // Alloca memoria per i punteggi
    inserisci_giocatore(nuovo_giocatore, &albero_giocatori); // Aggiungi il nuovo giocatore alla lista dei giocatori
    return nuovo_giocatore;

}

void invia_quiz_disponibili(struct Giocatore* giocatore) {
    int dim = 1024;
    char messaggio_di_errore[256];
    char buffer[dim]; // Buffer per i temi
    uint32_t dim_temi;
    uint32_t dim_temi_net;

    // Prima invio il numero di quiz disponibili
    uint32_t numero_di_quiz_disponibili_net = htonl(numero_di_quiz_disponibili); // Converto in formato di rete

    // Scrivo il messaggio di errore
    memset(messaggio_di_errore, 0, sizeof(messaggio_di_errore));
    snprintf(messaggio_di_errore, sizeof(messaggio_di_errore), "Errore nell'invio del numero dei quiz al client %s: ", giocatore->nickname);
    
    // Invia il numero di quiz disponibili al client
    send_all(giocatore->socket, (void*)&numero_di_quiz_disponibili_net, sizeof(numero_di_quiz_disponibili), gestisci_ritorno_recv_send_lato_server, messaggio_di_errore);

    // Ora invio i temi come una stringa unica il cui separatore è '\n'
    memset(buffer, 0, dim);
    for (uint32_t i = 0; i < numero_di_quiz_disponibili; ++i) {
        strcat(buffer, quiz_disponibili[i]);
        strcat(buffer, "\n"); // Uso "\n" come separatore
    }

    // Prima di inviare i temi, calcolo la lunghezza del buffer e la converto in formato di rete,
    // per poi inviarla al client
    dim_temi = strlen(buffer);
    dim_temi_net = htonl(dim_temi); // Converto in formato di rete
    send_all(giocatore->socket, (void*)&dim_temi_net, sizeof(dim_temi_net), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della lunghezza del buffer dei temi al client");

    // Ora invio il buffer contenente i temi
    send_all(giocatore->socket, (void*)buffer, dim_temi, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio dei temi al client");

}

struct Quiz_Completato* trova_giocatore_quiz_completato(struct Giocatore* giocatore, struct Quiz_Completato* curr) {
    // Funzione per trovare un giocatore nell'albero dei quiz completati
    struct Quiz_Completato* trovato = NULL;
    if (curr == NULL) {
        return NULL; // Se l'albero è vuoto, ritorna NULL
    }
    if (strcmp(curr->giocatore->nickname, giocatore->nickname) == 0) {
        return curr; // Trovato il giocatore con il nickname corrispondente
    }
    trovato = trova_giocatore_quiz_completato(giocatore, curr->left);
    return trovato ? trovato : trova_giocatore_quiz_completato(giocatore, curr->right);
}


int quiz_gia_giocato(struct Giocatore* giocatore, uint8_t quiz_scelto) {
    // Ritorna 1 se il giocatore ha gia giocato al quiz
    // Ritorna 0 altrimenti
    return (quiz_completati[quiz_scelto - 1] != NULL && trova_giocatore_quiz_completato(giocatore, quiz_completati[quiz_scelto - 1]) != NULL);
}


uint8_t ricevi_quiz_scelto(struct Giocatore* giocatore) {
    uint8_t quiz_scelto = 0;
    uint8_t ack; // Variabile per gestire l'ACK
    do {
        // Ricevo il quiz scelto dal client
        recv_all(giocatore->socket, (void*)&quiz_scelto, sizeof(quiz_scelto), gestisci_ritorno_recv_send_lato_server, "Errore nella ricezione del quiz scelto dal client");
        if (quiz_scelto < 1 || quiz_scelto > numero_di_quiz_disponibili || quiz_gia_giocato(giocatore, quiz_scelto)) {
            ack = 0; // Il quiz scelto non è valido
            invia_ack(giocatore->socket, ack); // Invia un ACK negativo al client
        }
        else {
            ack = 1; // Il quiz scelto è valido
            invia_ack(giocatore->socket, ack); // Invia un ACK positivo al client
            printf("Il client %s ha scelto il quiz: %s\n", giocatore->nickname, quiz_disponibili[quiz_scelto - 1]);
        }
    } while (ack == 0); // Continua a ricevere finché non ricevo un quiz valido
    return quiz_scelto; // Ritorno il numero del quiz scelto
}

void string_to_lower(char *s) {
    // Funzione per convertire una stringa in minuscolo
    for (int i = 0; s[i] != '\0'; i++) {
        s[i] = tolower((unsigned char)s[i]);
    }
}





void inserisci_quiz_completato_ricorsivo(struct Giocatore* curr, struct Quiz_Completato** quiz_completato) {
    // Funzione per inserire un giocatore nell'albero dei quiz completati
    if (*quiz_completato == NULL) {
        // Se l'albero è vuoto, inizializza l'albero con il nuovo giocatore
        *quiz_completato = malloc(sizeof(struct Quiz_Completato));
        (*quiz_completato)->giocatore = curr;
        (*quiz_completato)->left = NULL;
        (*quiz_completato)->right = NULL;
    } else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine alfabetico di nickname
        if (strcmp(curr->nickname, (*quiz_completato)->giocatore->nickname) < 0) {
            inserisci_quiz_completato_ricorsivo(curr, &(*quiz_completato)->left);
        } else {
            inserisci_quiz_completato_ricorsivo(curr, &(*quiz_completato)->right);
        }
    }

}

void inserisci_quiz_completato(struct Giocatore* curr, struct Quiz_Completato** quiz_completato, pthread_mutex_t* mutex) {
    pthread_mutex_lock(mutex); // Blocca l'accesso all'albero dei quiz completati
    // printf("Inserisco il giocatore %s nell'albero dei quiz completati\n", curr->nickname);
    inserisci_quiz_completato_ricorsivo(curr, quiz_completato);
    pthread_mutex_unlock(mutex); // Sblocca l'accesso all'albero dei quiz
    // printf("Giocatore %s inserito nell'albero dei quiz completati\n", curr->nickname);
}


struct Punteggio* crea_punteggio(struct Giocatore* curr) {
    // Funzione per creare un nuovo Punteggio
    struct Punteggio* nuovoPunteggio = malloc(sizeof(struct Punteggio));
    nuovoPunteggio->giocatore = curr;
    nuovoPunteggio->left = NULL;
    nuovoPunteggio->right = NULL;
    return nuovoPunteggio;
}

void salva_quiz_completato(struct Giocatore* giocatore, char quiz_scelto) {
    inserisci_quiz_completato(giocatore, &quiz_completati[quiz_scelto - 1], &mutex_quiz_completati[quiz_scelto - 1]); // Inserisco il giocatore nell'albero dei giocatori
}

void inserisci_punteggio_iterativo(struct Giocatore* giocatore, uint8_t quiz_scelto) {
    // Funzione per inserire un nuovo punteggio nell'albero dei punteggi di indice quiz_scelto
    // L'albero è ordinato in ordine di punteggio
    // A parità di punteggio, inserisco a sinistra se il nickname del nuovo giocatore è "maggiore" di quello del giocatore corrente,
    // a destra altrimenti. Faccio così per ottenere la stampa post-order in modo che i punteggi siano ordinati in ordine decrescente
    // di punteggio e, a parità di punteggio, in ordine alfabetico di nickname.

    if (vettore_punteggi[quiz_scelto - 1] == NULL) {
        // Se l'albero dei punteggi è vuoto, inizializza l'albero con il nuovo giocatore
        vettore_punteggi[quiz_scelto - 1] = crea_punteggio(giocatore);
    }
    else {
        // Altrimenti, inserisci il nuovo giocatore nell'albero in ordine di punteggio
        // e, a parità di punteggio, in ordine alfabetico di nickname.
        struct Punteggio* curr_p = vettore_punteggi[quiz_scelto - 1];
        while (1) {
            if (giocatore->punteggio[quiz_scelto - 1] > curr_p->giocatore->punteggio[quiz_scelto - 1]) {
                // Se il nuovo punteggio è maggiore del punteggio corrente,
                // inserisco il nuovo giocatore a destra di curr_p.
                if (curr_p->right == NULL) {
                    curr_p->right = crea_punteggio(giocatore); // Inserisco a destra
                    return;
                }
                // Altrimenti, scendo nel sottoalbero destro
                curr_p = curr_p->right; // Scendo nel sottoalbero destro
            }
            else if (giocatore->punteggio[quiz_scelto - 1] < curr_p->giocatore->punteggio[quiz_scelto - 1]) {
                // Altrimenti, continuo a cercare nel sottoalbero sinistro
                if (curr_p->left == NULL) {
                    // Se il sotto albero sinistro è vuoto, inserisco il nuovo punteggio a sinistra
                    curr_p->left = crea_punteggio(giocatore);
                    return;
                }
                // Altrimenti, scendo nel sottoalbero sinistro
                curr_p = curr_p->left; // Scendo nel sottoalbero sinistro
            }
            else {
                // Se il punteggio è uguale, inserisco in ordine alfabetico di nickname
                if (strcmp(giocatore->nickname, curr_p->giocatore->nickname) > 0) {
                    // Se il nickname del giocatore corrente è "maggiore", inserisco a sinistra
                    if (curr_p->left == NULL) {
                        curr_p->left = crea_punteggio(giocatore);
                        return;
                    }
                    curr_p = curr_p->left; // Scendo nel sottoalbero sinistro
                } else {
                    // Altrimenti, inserisco a destra
                    if (curr_p->right == NULL) {
                        curr_p->right = crea_punteggio(giocatore);
                        return;
                    }
                    curr_p = curr_p->right; // Scendo nel sottoalbero destro
                }
            }
        }
    }
}


void aggiorna_punteggio(struct Giocatore* giocatore, uint8_t quiz_scelto) {
    pthread_mutex_lock(&mutex_punteggi[quiz_scelto - 1]); // Blocca l'accesso all'albero dei punteggi
    // printf("Aggiorno il punteggio del giocatore %s nel quiz %d con il nuovo punteggio: %d\n", curr->nickname, quiz_scelto, nuovo_punteggio);
    // Rimuovo il giocatore prima di effettuare la ricorsione d'inserimento per evitare problemi durante la ricorsione
    // Infatti, l'albero dei punteggi è ordinato in ordine di punteggio e, solo a parità di punteggio, in ordine alfabetico di nickname.
    // Quindi, se il giocatore è già presente, devo rimuoverlo prima di inserirlo con il nuovo punteggio, visto che la
    // ricerca del giocatore avviene per punteggio, e non per nickname
    // Se il giocatore è già presente, ovviamente ne incremento il punteggio.
    // Per farlo, cancello il nodo associato a tale giocatore e creo un nuovo nodo con il punteggio incrementato.
    // Devo fare così, altrimenti romperei l'ordinamento.
    if (giocatore->punteggio[quiz_scelto - 1] >= 1) {
        // Se il nuovo punteggio è maggiore di 1, il giocatore era già presente nell'albero dei punteggi, quindi lo rimuovo
        // Rimuovo il giocatore dall'albero dei punteggi. Per farlo, devo trovare il nodo dell'albero dei punteggi associato al quiz scelto.
        // Per farlo, mi basta sapere chi è il giocatore corrente il suo punteggio presente nell'albero dei punteggi, che è
        // nuovo_punteggio - 1, visto che lo sto aggiornando.
        rimuovi_giocatore_da_punteggi_ricorsivo(&vettore_punteggi[quiz_scelto - 1], giocatore, quiz_scelto);
    }
    ++giocatore->punteggio[quiz_scelto - 1]; // Incremento il punteggio del giocatore per il quiz scelto
    inserisci_punteggio_iterativo(giocatore, quiz_scelto);
    pthread_mutex_unlock(&mutex_punteggi[quiz_scelto - 1]); // Sblocca l'accesso all'albero dei punteggi
    // printf("Punteggio aggiornato correttamente.\n");
}





void crea_stringa_punteggio(struct Punteggio* punteggio, uint8_t quiz_selezionato, char* buffer, uint32_t buf_size, char* start) {
    // Funzione per inviare il punteggio al client
    // Invia il punteggio del quiz al client in ordine di punteggio e, a parità di punteggio, in ordine alfabetico di nickname
    // Per farlo, faccio una visita post-order dell'albero dei punteggi, così da inviare i punteggi in ordine decrescente
    if (punteggio == NULL) {
        return; // Se il punteggio è NULL, non invio nulla
    }
    crea_stringa_punteggio(punteggio->right, quiz_selezionato, buffer, buf_size, start); // Invia il punteggio del sottoalbero destro

    // Salva il punteggio
    snprintf(buffer + *start, buf_size, "%d|",  punteggio->giocatore->punteggio[quiz_selezionato - 1]);
    // Salva il nickname del giocatore associato a tale punteggio
    snprintf(buffer + *start + strlen(buffer + *start), buf_size - *start, "%s\n", punteggio->giocatore->nickname);
    *start += strlen(buffer + *start); // Aggiorna l'indice di partenza per il prossimo punteggio

    crea_stringa_punteggio(punteggio->left, quiz_selezionato, buffer, buf_size, start); // Invia il punteggio del sottoalbero sinistro
}

void invia_punteggio_di_ogni_tema(int sd) {
    // Funzione per inviare il punteggio di ogni tema al client
    // Per ogni tema, invia una stringa col seguente formato:
    // "<numero_tema>\n<punteggio>|<nickname>\n"
    // Per capire se ho un numero_tema o un punteggio con nickname, basta vedere se nella riga ho un '|'
    // Finché non si passa al prossimo tema, non aggiungerò altri <numero_tema>\n
    char buffer[1024];
    char start = 0;
    uint32_t lunghezza_buffer; // Lunghezza del buffer da inviare al client
    memset(buffer, 0, sizeof(buffer)); // Inizializza il buffer
    for (uint8_t quiz_selezionato = 1; quiz_selezionato <= numero_di_quiz_disponibili; ++quiz_selezionato) {
        if (vettore_punteggi[quiz_selezionato - 1] == NULL) {
            continue; // Se non ci sono punteggi per questo tema (quindi nessun giocatore ha ancora risposto a domande di questo quiz), salta
        }
        snprintf(buffer + start, sizeof(buffer) - start, "%d\n", quiz_selezionato); // Aggiungo il numero del tema
        start += strlen(buffer + start); // Aggiorno l'indice di partenza
        crea_stringa_punteggio(vettore_punteggi[quiz_selezionato - 1], quiz_selezionato, buffer, sizeof(buffer), &start); // Aggiungo i punteggi (con i rispettivi nickname) del tema corrente al buffer
    }
    // Invio la lunghezza della stringa al client
    lunghezza_buffer = htonl(strlen(buffer)); // Converto la lunghezza in formato di rete
    send_all(sd, (void*)&lunghezza_buffer, sizeof(lunghezza_buffer), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della lunghezza del buffer dei punteggi al client");
    // Invio la stringa al client                  
    send_all(sd, (void*)buffer, strlen(buffer), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio dei punteggi del tema al client");
}

void invia_risposta(int sd, const char* risposta) {
    // Funzione per inviare la risposta del server al client
    // Invio la lunghezza della risposta al client (sta in un uint8_t)
    uint8_t dimensione_risposta = strlen(risposta);
    // printf("Invio della dimensione della risposta: %d\n", dimensione_risposta);
    send_all(sd, (void*)&dimensione_risposta, sizeof(dimensione_risposta), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della dimensione della risposta al client");
    // Invia la risposta al client
    // printf("Invio della risposta al client: %s\n", risposta);
    send_all(sd, (void*)risposta, dimensione_risposta, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della risposta del server al client");
    
}

void invia_risposta_corretta(int sd) {
    invia_risposta(sd, "Risposta corretta");
}

void invia_risposta_errata(int sd) {
    invia_risposta(sd, "Risposta errata");
}


void gestisci_risposta_corretta(struct Giocatore* giocatore, const char* risposta_client, uint8_t quiz_scelto) {
    // Funzione per gestire la risposta corretta del client
    // Incrementa il punteggio del giocatore, invia la risposta corretta al client e stampa l'interfaccia aggiornata
    printf("Risposta corretta del client %s: %s\n", giocatore->nickname, risposta_client);
    invia_risposta_corretta(giocatore->socket); // Invia la risposta corretta al client
    aggiorna_punteggio(giocatore, quiz_scelto); // Incrementa il punteggio del client
    stampa_interfaccia(); // Stampa l'interfaccia aggiornata
}


void* gestisci_connessione(void* arg) {
    int cl_sd = *(int*)arg;
    free(arg); // Libero la memoria allocata per il socket
    uint8_t quiz_scelto;
    char domanda[1024], risposta_client[1024], risposta_server[1024]; // Buffer per le domande
    uint8_t dimensione_domanda;
    uint8_t dimensione_risposta;
    uint8_t domanda_non_ancora_inviata = 0; // Variabile per non inviare più volte la stessa domanda al client (quando il client invia "show score" o "endquiz")
    struct Giocatore* giocatore = crea_giocatore(cl_sd);
    int prima_iterazione = 1; // Variabile per indicare se è la prima iterazione del ciclo
    stampa_interfaccia(); // Stampa l'interfaccia grafica del server
    // A questo punto, finché il client non interrompe la connessione (a meno di errori):
    // 1. Invia i quiz disponibili al client (solo la prima volta)
    // 2. Riceve il quiz scelto dal client
    // 3. Inizia il quiz con il client
    while(1) {
        int c = 0;
        if (prima_iterazione) {
            invia_quiz_disponibili(giocatore); // Funzione per inviare i quiz disponibili al giocatore
            prima_iterazione = 0; // Setto a 0 per non inviare più i quiz disponibili
        }
        quiz_scelto = ricevi_quiz_scelto(giocatore); // Funzione per ricevere il quiz scelto dal giocatore
        // Adesso il client può iniziare a giocare con il quiz scelto.
        uint32_t domande_inviate = 0; // Contatore delle domande inviate
        char filename[256];
        memset(filename, 0, sizeof(filename)); // Inizializzo il buffer del nome del file
        snprintf(filename, sizeof(filename), "quiz/%d.txt", quiz_scelto); // Costruisco il nome del file del quiz scelto
        FILE *fp = fopen(filename, "r");
        if (fp == NULL) {
            perror("Errore nell'apertura del file");
            close(cl_sd);
            pthread_exit(NULL);
        }
        domanda_non_ancora_inviata = 1; // Setto la variabile per indicare che la domanda non è ancora stata inviata
        // Leggo le domande dal file e le invio al client
        while (c != EOF && domande_inviate < DOMANDE_PER_TEMA) {
            // Invio, in ordine di scrittura, le domande del quiz scelto al client
            int i = 0;
            memset(domanda, 0, sizeof(domanda)); // Inizializzo il buffer della domanda
            // risposta_ricevuta serve per non cosniderare "endquiz" e "show score" come risposte valide
            if (domanda_non_ancora_inviata) {
                while ((c = fgetc(fp)) != EOF) { // Leggo il file fino alla fine
                    domanda[i++] = c; // Aggiungo il carattere al buffer della domanda
                    // printf("%c", c); // Stampo il carattere letto (per debug)
                    if (c == '?') {
                        break;  // Trovato e inserito nel buffer il carattere '?', rompo il ciclo
                    }
                }
                // Invio la lunghezza della domanda al client
                // printf("Dimensione della domanda: %d\n", i);
                dimensione_domanda = i; // La dimensione della domanda è pari al numero di caratteri letti
                send_all(cl_sd, (void*)&dimensione_domanda, sizeof(dimensione_domanda), gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della dimensione della domanda al client");
                // Invio la domanda al client
                printf("Invio domanda al client %s: %s\n", giocatore->nickname, domanda);
                send_all(cl_sd, (void*)domanda, i, gestisci_ritorno_recv_send_lato_server, "Errore nell'invio della domanda al client");
            }
            // Ricevo la dimensione della risposta dal client (tale dimensione è un uint8_t)
            recv_all(cl_sd, &dimensione_risposta, sizeof(dimensione_risposta), gestisci_ritorno_recv_send_lato_server, "Errore nella ricezione della dimensione della risposta dal client");
            // printf("Dimensione della risposta: %d\n", dimensione_risposta);
            // Ricevo la risposta del client
            memset(risposta_client, 0, sizeof(risposta_client)); // Inizializzo il buffer della risposta
            recv_all(cl_sd, (void*)risposta_client, dimensione_risposta, gestisci_ritorno_recv_send_lato_server, "Errore nella ricezione della risposta dal client");
            // printf("Ricevuta risposta dal client %s: %s\n", giocatore->nickname, risposta_client);
            if (strcmp(risposta_client, "show score") == 0) {
                invia_punteggio_di_ogni_tema(cl_sd);
                domanda_non_ancora_inviata = 0; // Segno che il client ha già ricevuto la domanda
                continue;
            }
            if (strcmp(risposta_client, "endquiz") == 0) {
                invia_ack(cl_sd, 1); // Invia un ACK per terminare il quiz
                printf("Il client %s ha terminato il gioco\n\n", giocatore->nickname);
                elimina_giocatore(giocatore); // Elimina il giocatore dalla lista
                stampa_interfaccia(); // Stampa l'interfaccia aggiornata
                fclose(fp); // Chiudo il file
                close(cl_sd); // Chiudo il socket del client
                pthread_exit(NULL); // Termina il thread
            }
            string_to_lower(risposta_client); // Converto la risposta del client in minuscolo
            c = fgetc(fp); // Leggo il file per scartare lo spazio tra la domanda e le risposte
            i = 0; // Resetto l'indice per la risposta
            memset(risposta_server, 0, sizeof(risposta_server)); // Inizializzo il buffer della risposta del server
            while ((c = fgetc(fp))) { // Leggo il file fino alla fine (la fine della risposta è segnata da un carattere '\n'. Le varie risposte valide sono separate da '|')
                if (c == '|') { // Ho letto una risposta (valida) dal file
                    // Se la domanda ammette più risposte valide, le leggo una alla volta.
                    // Nel caso di più risposte valide, se la risposta letta non è l'ultima, devo togliere lo spazio finale (quello prima di '|')
                    risposta_server[i - 1] = risposta_server[i - 1] == ' ' ? '\0' : risposta_server[i - 1];

                    if (strcmp(risposta_client, risposta_server) == 0) {
                        // Se trovo il carattere '|' e la risposta del client corrisponde alla risposta del server,
                        // invio un messaggio di successo al client
                        gestisci_risposta_corretta(giocatore, risposta_client, quiz_scelto);
                        break;
                    }
                    memset(risposta_server, 0, sizeof(risposta_server)); // Resetto il buffer della risposta del server, così da fare il test con un'altra risposta valida
                    i = 0; // Resetto l'indice per la risposta del server
                    c = fgetc(fp); // Leggo il prossimo carattere (per scartare lo spazio tra le risposte)
                    continue;
                }
                else if (c == '\n' || c == EOF) { // Ho letto l'ultima risposta (valida) dal file per quella domanda (che potrebbe essere l'ultima risposta per tutte le domande di quel file)
                    printf("Risposta del server: %s\n", risposta_server); // Stampo la risposta del server (per debug)
                    // Sempre nel caso di più risposte valide, ora 
                    if (strcmp(risposta_client, risposta_server) == 0) {
                        // Se la risposta del client corrisponde alla risposta del server, aggiorno il punteggio
                        gestisci_risposta_corretta(giocatore, risposta_client, quiz_scelto);
                    } else {
                        printf("Risposta errata del client %s: %s\n", giocatore->nickname, risposta_client);
                        invia_risposta_errata(cl_sd); // Invia un messaggio di errore al client
                    }
                    break;  // Trovato il carattere '\n', interrompe, così da passare alla prossima domanda
                }
                risposta_server[i++] = c; // Aggiungo il carattere al buffer della risposta
                // printf("Risposta del server: %s\n", risposta_server); // Stampo la risposta del server
            }
            if (c == EOF) {
                // Se ho raggiunto la fine del file, interrompo il ciclo
                printf("Fine del file raggiunta per il quiz %d\n", quiz_scelto);
                break; // Esco dal ciclo delle domande
            }
            domanda_non_ancora_inviata = 1; // Setto la variabile per la prossima domanda
            ++domande_inviate; // Incremento il contatore delle domande inviate
            // printf("Domanda %d inviata al client %s\n", domande_inviate, giocatore->nickname);
            while (c != '\n' && c != EOF) {
                // Scarto il resto della riga (fino al carattere '\n' o EOF) per passare alla prossima domanda
                c = fgetc(fp);
                // printf("Scarto carattere: %c\n", c); // Stampo il carattere scartato (per debug)
            }

            // printf("Fine domanda %d\n", domande_inviate);
            
        }
        // Questo client ha completato il quiz
        // printf("Il client %s ha completato il quiz %d con un punteggio di %d\n", giocatore->nickname, quiz_scelto, punteggio);
        salva_quiz_completato(giocatore, quiz_scelto);
        fclose(fp); // Chiudo il file
        stampa_interfaccia();
    }
    // A regola non dovrei mai arrivare qui ma, per sicurezza, chiudo il socket del client e termino il thread
    elimina_giocatore(giocatore); // Libero la memoria dinamica allocata per il giocatore
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
    quiz_completati = malloc(numero_di_quiz_disponibili * sizeof(struct Quiz_Completato*)); // Alloco il vettore dei quiz completati
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
    int ret, sd;
    socklen_t len;
    pthread_t thread_id;
    struct sockaddr_in my_addr, cl_addr;
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
    pthread_mutex_init(&mutex_albero_giocatori, NULL); // Inizializzo il mutex per la mutua esclusione dell'albero dei giocatori
    ottieni_quiz_disponibili(); // Ottengo dinamicamente i quiz disponibli leggendo dal file "quiz/info.txt"
    stampa_interfaccia(); // Stampo, adesso, l'interfaccia (solo dopo aver ottenuto i quiz)
    while(1) {
        int *cl_sd = malloc(sizeof(int)); // Alloco memoria per il socket del client (lo metto sullo heap per poterlo passare al thread)
        if (cl_sd == NULL) {
            perror("Errore nella malloc");
            close(*cl_sd);
        }
        *cl_sd = accept(sd, (struct sockaddr*)&cl_addr, &len);
        if (*cl_sd < 0) {
            perror("Errore nell'accept");
            free(cl_sd);
        }
        ret = pthread_create(&thread_id, NULL, gestisci_connessione, (void*)cl_sd);
        if (ret != 0) {
            perror("Errore nella creazione del thread");
            free(cl_sd);
        }
        
    }
}