# Trivia Quiz - Progetto di Reti Informatiche

## Descrizione

Questo progetto implementa un gioco Trivia Quiz client-server in C.  
Il server gestisce quiz a tema, punteggi e la classifica dei giocatori.  
I client si collegano, scelgono un tema, rispondono alle domande e visualizzano i punteggi.

---

## Requisiti

- Linux
- `gcc`
- `make`
- `gnome-terminal` (o `xterm`/`konsole`)
- Librerie standard C (pthread, socket)

---

## Installazione

1. Clona o scarica la repository.
2. Assicurati di avere i requisiti installati.  
   Puoi usare lo script `start.sh` che installa automaticamente `make`, `gcc` e `gnome-terminal` se non presenti.

---

## Compilazione

Da terminale, nella cartella del progetto:
```sh
make
```

---

## Avvio rapido

Usa lo script:
```sh
./start.sh
```
Questo comando:
- Compila il progetto
- Avvia il server in un nuovo terminale
- Avvia il client in un altro terminale

---

## Utilizzo

1. Il server attende connessioni sulla porta `4242`.
2. Il client si collega, sceglie un tema e risponde alle domande.
3. I punteggi vengono aggiornati e visualizzati in tempo reale.

---

## Struttura del progetto

- `server.c` — Codice sorgente del server
- `client.c` — Codice sorgente del client
- `src/` — Funzioni di utilità (send/recv, stampa, ecc.)
- `include/` — File header
- `Makefile` — Compilazione automatica
- `start.sh` — Script di avvio rapido

---

## Note

- Puoi avviare più client contemporaneamente.
- Il server gestisce la classifica e la sincronizzazione dei punteggi.
- Per modificare quiz e domande, aggiorna i file di configurazione.

---

## Autori

- Antonio Querci

---

## Licenza

Questo progetto è distribuito con licenza MIT.