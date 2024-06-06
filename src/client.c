#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>


int sockfd;
struct sockaddr_in address;
pthread_t senderThread;
pthread_t receiverThread;

void shutdownClient() {
    printf("Shutting down...\n");
    char *msg = "QUIT";
    write(sockfd, msg, strlen(msg));
    pthread_cancel(senderThread);
    pthread_cancel(receiverThread);
}

void *sender() {
    while (1) {
        char buffer[1024] = {0};
        fgets(buffer, sizeof(buffer), stdin);
        
        int sent = write(sockfd, buffer, strlen(buffer) - 1);
        if (sent == -1)  {
            fprintf(stderr, "Socket write error\n");
            pthread_cancel(receiverThread);
            return NULL;
        }
    }
}

void *receiver() {
    while (1) {
        char buffer[1024] = {0};
        int received;
        if ((received = read(sockfd, buffer, sizeof(buffer) - 1)) == -1) {
            fprintf(stderr, "Socket read error\n");
            pthread_cancel(senderThread);
            return NULL;
        }
        if (received == 1 && buffer[0] == 0) break;

        printf("%s", buffer);
        fflush(stdout);
    }
}


int main() {
    signal(SIGINT, shutdownClient);

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sockfd == -1) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(1337);

    if (connect(sockfd, &address, sizeof(address)) == -1) {
        fprintf(stderr, "Connection failure\n");
        return 1;
    }

    pthread_create(&receiverThread, NULL, receiver, NULL);
    pthread_create(&senderThread, NULL, sender, NULL);
    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);

    close(sockfd);
}