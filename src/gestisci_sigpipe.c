void gestisci_sigpipe(int sig) {
    // SIGPIPE viene inviato dal kernel quando il processo prova a scrivere (quin di fa send o write)
    // su un socket la cui controparte ha già chiuso la connessione.
    (void)sig; // Sopprime il warning per parametro non usato
}
