#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define BUF_SIZE 1024

int main(void)
{
    int sockfd;
    struct sockaddr_in server;
    char buf[BUF_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return 1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &server.sin_addr) != 1) {
        fprintf(stderr, "Bad IP address\n");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) != 0) {
        fprintf(stderr, "Could not connect to server\n");
        close(sockfd);
        return 1;
    }

    printf("Connected. Waiting for other players...\n");

    while (1) {
        ssize_t bytes = recv(sockfd, buf, BUF_SIZE - 1, 0);

        if (bytes == 0) {
            printf("Server closed the connection.\n");
            break;
        }

        if (bytes < 0) {
            perror("recv");
            break;
        }

        buf[bytes] = '\0';
        fputs(buf, stdout);

        if (strstr(buf, "INPUT_REQUIRED")) {
            char input[BUF_SIZE];

            if (fgets(input, sizeof(input), stdin) == NULL)
                break;

            if (send(sockfd, input, strlen(input), 0) < 0) {
                perror("send");
                break;
            }
        }
    }

    close(sockfd);
    return 0;
}
