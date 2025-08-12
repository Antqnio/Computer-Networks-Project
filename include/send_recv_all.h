#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stddef.h>     // size_t
#include <sys/types.h>  // ssize_t

/**
 * Legge esattamente len byte dal socket sock.
 * Ritorna il numero di byte letti o -1 su errore.
 */
ssize_t recv_all(int sock, void *buf, size_t len, void (*gestisci_ritorno_recv)(int, int, const char*), const char* errore_msg);

/**
 * Invia esattamente len byte sul socket sock.
 * Ritorna il numero di byte inviati o -1 su errore.
 */
ssize_t send_all(int sock, const void *buf, size_t len, void (*gestisci_ritorno_send)(int, int, const char*), const char* errore_msg);

#endif // NET_UTILS_H
