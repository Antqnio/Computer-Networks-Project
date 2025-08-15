#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

/**
 * Funzione per leggere esattamente 'len' byte da un socket.
 * Ritorna il numero di byte letti o -1 su errore.
 */
ssize_t recv_all(int sock, void *buf, size_t len, void (*gestisci_ritorno_recv)(int, int, const char*), const char* errore_msg) {
    size_t total = 0;
    char *p = buf;
    while (total < len) {
        ssize_t ret = recv(sock, p + total, len - total, 0);
        if (ret <= 0) {
            gestisci_ritorno_recv(ret, sock, errore_msg); // Errore o connessione chiusa
            if (ret == 0) {
                return 0; // Connessione chiusa
            }
        }
        // Se siamo qui, ho ricevuto 'ret' byte
        total += ret;
    }
    return total; // Ritorna il numero totale di byte ricevuti
}

/**
 * Funzione per inviare esattamente 'len' byte su un socket.
 * Ritorna il numero di byte inviati o -1 su errore.
 */
ssize_t send_all(int sock, const void *buf, size_t len, void (*gestisci_ritorno_send)(int, int, const char*), const char* errore_msg) {
    size_t total = 0;
    const char *p = buf;
    while (total < len) {
        ssize_t s = send(sock, p + total, len - total, 0);
        if (s <= 0) {
            gestisci_ritorno_send(s, sock, errore_msg); // Errore o connessione chiusa
            if (s == 0) {
                return total; // Connessione chiusa
            }
        }
        total += s;
    }
    return total; // Ritorna il numero totale di byte inviati
}
