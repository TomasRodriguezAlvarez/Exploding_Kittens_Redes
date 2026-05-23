#include <stdio.h>
#include <stdlib.h>

int main() {
    FILE *file = fopen("game_log.txt", "r");

    if (file == NULL) {
        perror("No se pudo abrir game_log.txt");
        return 1;
    }

    char line[512];

    printf("===== REPLAY / AUDITORIA DE PARTIDA =====\n\n");

    while (fgets(line, sizeof(line), file)) {
        printf("%s", line);
    }

    fclose(file);

    printf("\n===== FIN DEL REPLAY =====\n");

    return 0;
}