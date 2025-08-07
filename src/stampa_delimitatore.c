#include <stdio.h>
#define NUMERO_DELIMITATORI 30

void stampa_delimitatore() {
    for (int i = 0; i < NUMERO_DELIMITATORI; ++i) {
        printf("+");
    }
    printf("\n");
}