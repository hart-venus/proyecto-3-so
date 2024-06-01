#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <asm-generic/socket.h>

#define PORT 8889
#define BUFFER_SIZE 1024

void start_server(const char *directory);
void start_client(const char *directory, const char *server_ip);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <directorio> [<IP del servidor>]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc == 2) {
        // Modo servidor
        start_server(argv[1]);
    } else if (argc == 3) {
        // Modo cliente
        start_client(argv[1], argv[2]);
    } else {
        fprintf(stderr, "Uso incorrecto: %s <directorio> [<IP del servidor>]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    return 0;
}

void start_server(const char *directory) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // Crear socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    // Adjuntar socket al puerto 8889
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Error en setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Enlazar el socket al puerto
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones entrantes
    if (listen(server_fd, 3) < 0) {
        perror("Error en listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor esperando conexiones en el puerto %d...\n", PORT);

    // Aceptar una conexión entrante
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Error en accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Conexión aceptada\n");

    // Aquí se manejaría la comunicación con el cliente
    // ...

    close(new_socket);
    close(server_fd);
}

void start_client(const char *directory, const char *server_ip) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Crear socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convertir la dirección IP del servidor
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida o no soportada");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Conectar al servidor
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error en connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Conectado al servidor %s en el puerto %d\n", server_ip, PORT);

    // Aquí se manejaría la comunicación con el servidor
    // ...

    close(sock);
}
