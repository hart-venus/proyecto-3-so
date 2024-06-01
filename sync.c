#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

#define PORT 8889
#define BUFFER_SIZE 1024
#define MAX_FILES 100
#define MAX_FILENAME 256
#define MAX_FILESIZE 1024 * 1024 * 10 // 10 MB

typedef struct {
    char filename[MAX_FILENAME];
    time_t last_modified;
    size_t size;
} FileInfo;


void send_file(int sock, const char *directory, const char *filename) {
    char filepath[MAX_FILENAME];
    snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);

    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        perror("fopen");
        return;
    }

    fseek(fp, 0, SEEK_END);
    size_t filesize = ftell(fp);
    rewind(fp);

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "SEND_FILE %s %zu", filename, filesize);
    send(sock, buffer, strlen(buffer), 0);

    while (!feof(fp)) {
        size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, fp);
        send(sock, buffer, bytes_read, 0);
    }

    fclose(fp);
}

void receive_file(int sock, const char *directory, const char *filename, size_t filesize) {
    char filepath[MAX_FILENAME];
    snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);

    FILE *fp = fopen(filepath, "wb");
    if (fp == NULL) {
        perror("fopen");
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_received = 0;

    while (bytes_received < filesize) {
        ssize_t bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            break;
        }
        fwrite(buffer, 1, bytes_read, fp);
        bytes_received += bytes_read;
    }

    fclose(fp);
}

void send_delete(int sock, const char *filename) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "DELETE_FILE %s", filename);
    send(sock, buffer, strlen(buffer), 0);
}

void delete_file(const char *directory, const char *filename) {
    char filepath[MAX_FILENAME];
    snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);
    remove(filepath);
}



void sync_files(int sock, const char *directory) {
    DIR *dir;
    struct dirent *entry;
    char filepath[MAX_FILENAME];
    FileInfo local_files[MAX_FILES], prev_files[MAX_FILES];
    int num_local_files = 0, num_prev_files = 0;

    // Leer la lista de archivos locales
    dir = opendir(directory);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            snprintf(filepath, sizeof(filepath), "%s/%s", directory, entry->d_name);
            struct stat st;
            if (stat(filepath, &st) == 0) {
                strncpy(local_files[num_local_files].filename, entry->d_name, MAX_FILENAME);
                local_files[num_local_files].last_modified = st.st_mtime;
                local_files[num_local_files].size = st.st_size;
                num_local_files++;
            }
        }
    }
    closedir(dir);

    // Leer la lista de archivos de la corrida anterior
    FILE *fp = fopen("prev_files.txt", "r");
    if (fp != NULL) {
        char line[MAX_FILENAME + 50];
        while (fgets(line, sizeof(line), fp) != NULL) {
            sscanf(line, "%s %ld %zu", prev_files[num_prev_files].filename,
                   &prev_files[num_prev_files].last_modified,
                   &prev_files[num_prev_files].size);
            num_prev_files++;
        }
        fclose(fp);
    }

    // Comparar los archivos locales con los de la corrida anterior
    for (int i = 0; i < num_local_files; i++) {
        int found = 0;
        for (int j = 0; j < num_prev_files; j++) {
            if (strcmp(local_files[i].filename, prev_files[j].filename) == 0) {
                found = 1;
                if (local_files[i].last_modified != prev_files[j].last_modified ||
                    local_files[i].size != prev_files[j].size) {
                    // El archivo ha sido modificado
                    send_file(sock, directory, local_files[i].filename);
                }
                break;
            }
        }
        if (!found) {
            // El archivo es nuevo
            send_file(sock, directory, local_files[i].filename);
        }
    }

    // Buscar archivos eliminados
    for (int i = 0; i < num_prev_files; i++) {
        int found = 0;
        for (int j = 0; j < num_local_files; j++) {
            if (strcmp(prev_files[i].filename, local_files[j].filename) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            // El archivo ha sido eliminado
            send_delete(sock, prev_files[i].filename);
        }
    }

    // Guardar la lista actualizada de archivos para la próxima corrida
    fp = fopen("prev_files.txt", "w");
    for (int i = 0; i < num_local_files; i++) {
        fprintf(fp, "%s %ld %zu\n", local_files[i].filename,
                local_files[i].last_modified, local_files[i].size);
    }
    fclose(fp);
}

void process_client_request(int client_sock, const char *directory) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "SEND_FILE", 9) == 0) {
            // El cliente está enviando un archivo
            char filename[MAX_FILENAME];
            size_t filesize;
            sscanf(buffer, "SEND_FILE %s %zu", filename, &filesize);
            receive_file(client_sock, directory, filename, filesize);
        } else if (strncmp(buffer, "DELETE_FILE", 11) == 0) {
            // El cliente está solicitando eliminar un archivo
            char filename[MAX_FILENAME];
            sscanf(buffer, "DELETE_FILE %s", filename);
            delete_file(directory, filename);
        }
    }

    // Enviar confirmación de finalización al cliente
    send(client_sock, "SYNC_COMPLETE", 13, 0);
}


void server_mode(const char *directory) {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_sock, 1) < 0) {
        perror("listen");
        exit(1);
    }

    printf("Server listening on port %d\n", PORT);

    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_size);
    if (client_sock < 0) {
        perror("accept");
        exit(1);
    }

    printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));

    process_client_request(client_sock, directory);

    close(client_sock);
    close(server_sock);
}

void client_mode(const char *directory, const char *server_ip) {
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("Connected to server: %s\n", server_ip);

    sync_files(sock, directory);

    close(sock);
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory> [server_ip]\n", argv[0]);
        exit(1);
    }

    if (argc == 2) {
        server_mode(argv[1]);
    } else {
        client_mode(argv[1], argv[2]);
    }

    return 0;
}