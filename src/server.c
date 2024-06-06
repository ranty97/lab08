#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <signal.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>

#define SERVER_ROOT "/home/dzmitry/Desktop/serverRoot"

struct connectionData {
    int working;
    int sockfd;
    char path[2048];
    char realpath[2048];
};

int sockfd;
struct sockaddr_in address;

char *ltrim(char *str) {
    while((*str) == ' ')
        str++;
    return str;
}

int startsWith(char *str, char *substr) {
    return !strncmp(str, substr, strlen(substr));
}

char *extractArgs(char *message) {
    char *args = strchr(message, ' ');
    return args ? args + 1 : "";
}

void list(struct connectionData *data) {
    struct dirent** pDirent;

    int total = scandir(data->realpath, &pDirent, NULL, NULL);

    for(int i = 0; i < total; i++) {
        struct stat fileinfo;

        if(!strcmp(".", pDirent[i]->d_name) || !strcmp("..", pDirent[i]->d_name)) {
            free(pDirent[i]);
            continue;
        }

        char path[1024];
        strcpy(path, data->realpath);
        strcat(path, "/");
        strcat(path, pDirent[i]->d_name);

        lstat(path, &fileinfo);

        char buffer[1024];

        if(S_ISDIR(fileinfo.st_mode)) {
            sprintf(buffer, "%s/\n", pDirent[i]->d_name);
        }
        else if(S_ISLNK(fileinfo.st_mode)) {
            char readlinkBuffer[1024] = {0};
            readlink(path, readlinkBuffer, sizeof(readlinkBuffer));
            if (startsWith(readlinkBuffer, SERVER_ROOT)) {
                sprintf(buffer, "%s --> %s\n", pDirent[i]->d_name, readlinkBuffer + strlen(SERVER_ROOT));
            } else {
                sprintf(buffer, "%s --> ?\n", pDirent[i]->d_name);
            }
        }
        else if(S_ISREG(fileinfo.st_mode)) {
            sprintf(buffer, "%s\n", pDirent[i]->d_name);
        }

        write(data->sockfd, buffer, strlen(buffer));
        free(pDirent[i]);
    }
    if (total != -1)
        free(pDirent);

}

void handleMessage(struct connectionData *data, char *message) {
    printf("Got message: %s\n", message);
    char *args = extractArgs(message);

    if (startsWith(message, "ECHO")) {
        strcat(args, "\n");
        write(data->sockfd, args, strlen(args));
        printf("Sent ECHO: %s\n", args);
        return;
    }
    if (startsWith(message, "QUIT")) {
        char *msg = "Bye bye.";
        write(data->sockfd, msg, strlen(msg));
        data->working = 0;
        printf("Said good bye to %d\n", data->sockfd);
        return;
    }
    if (startsWith(message, "INFO")) {
        char *msg = "Hello, im a server!\n";
        write(data->sockfd, msg, strlen(msg));
        printf("Gave info to %d\n", data->sockfd);
        return;
    }
    if (startsWith(message, "CD")) {
        if (startsWith("/", args)) {
            strcpy(data->realpath, SERVER_ROOT);
            strcpy(data->path, "/");
        } else {
            char new[1024];
            strcpy(new, data->realpath);
            strcat(new, "/");
            strcat(new, args);

            char path[1024];
            realpath(new, path);

            if (startsWith(path, SERVER_ROOT)) {
                strcpy(data->realpath, path);
                strcpy(data->path, path + strlen(SERVER_ROOT));
                strcat(data->path, "/");
            }
        }
        printf("Current dir for %d user is %s\n", data->sockfd, data->realpath);
        return;
    }
    if (startsWith(message, "LIST")) {
        printf("Current dir for %d user is %s\n", data->sockfd, data->realpath);
        list(data);
        printf("Successful LIST for user %d\n", data->sockfd);
        return;
    }
    if (startsWith(message, "@")) {
        char filename[1024];
        strcpy(filename, data->realpath);
        strcat(filename, "/");
        strcat(filename, ltrim(message + 1));

        FILE *file = fopen(filename, "r");
        printf("Opened: %s\n", filename);
        if (file == NULL) {
            printf("Failed to open file: %s\n", filename);
        }
        else {
            while (!feof(file)) {
                char buffer[1024] = {0};
                fgets(buffer, sizeof(buffer), file);

                if (strlen(buffer) <= 1 || feof(file)) {
                    break;
                }

                char hint[1024] = {0};

                strcpy(hint, data->path);
                strcat(hint, "> ");
                strcat(hint, buffer);
                write(data->sockfd, hint, strlen(hint));

                buffer[strlen(buffer) - 1] = 0;
                handleMessage(data, buffer);
            }
            fclose(file);
        }
    }
}

void handleConnection(struct connectionData *data) {
    while (data->working) {
        char hint[1024] = {0};
        char buffer[1024] = {0};

        strcpy(hint, data->path);
        strcat(hint, "> ");

        write(data->sockfd, hint, strlen(hint));

        if (read(data->sockfd, buffer, sizeof(buffer)) == -1) {
            fprintf(stderr, "Socket read error");
            data->working = 0;
        }
        handleMessage(data, ltrim(buffer));
    }

    printf("Connection %d closed.\n", data->sockfd);
    free(data);
}

void shutdownServer() {
    printf("Shutting down...");
    close(sockfd);
    exit(0);
}

void launchServer(uint16_t port) {
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sockfd == -1) {
        fprintf(stderr, "Failed to create socket\n");
        exit(1);
    }

    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(sockfd, &address, sizeof(address)) == -1) {
        fprintf(stderr, "Failed to bind socket\n");
        exit(1);
    }

    if (listen(sockfd, 10) == -1) {
        fprintf(stderr, "Failed to listen socket\n");
        exit(1);
    }

    printf("Waiting for connections on %hu port\n", port);

    while (1) {
        int connection = accept(sockfd, NULL, NULL);
        if (connection == -1) {
            fprintf(stderr, "Connection failure\n");
            continue;
        }
        printf("Accepted connection: %d\n", connection);

        pthread_t thread;
        struct connectionData *data = malloc(sizeof(struct connectionData));
        data->sockfd = connection;
        data->working = 1;
        strcpy(data->path, "/");
        strcpy(data->realpath, SERVER_ROOT);
        strcat(data->realpath, "/");

        pthread_create(&thread, NULL, handleConnection, data);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Use server [port]\n");
        return 0;
    }

    uint16_t port;

    if (sscanf(argv[1], "%hu", &port) != 1) {
        printf("Port should be a positive decimal");
    }

    signal(SIGINT, shutdownServer);

    launchServer(port);
}