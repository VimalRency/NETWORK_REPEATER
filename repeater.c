
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define INPUT_PORT 5000
#define OUTPUT_PORT 6000
#define BUFFER_SIZE 2048

int main(void)
{
    int sockfd;
    char buffer[BUFFER_SIZE];

    struct sockaddr_in input_addr;
    struct sockaddr_in output_addr;

    ssize_t bytes_received;
    ssize_t bytes_sent;

    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    /* Configure input address */
    memset(&input_addr, 0, sizeof(input_addr));

    input_addr.sin_family = AF_INET;
    input_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    input_addr.sin_port = htons(INPUT_PORT);

    /* Bind socket to input port */
    if (bind(sockfd, (struct sockaddr *)&input_addr,
             sizeof(input_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Configure output address */
    memset(&output_addr, 0, sizeof(output_addr));

    output_addr.sin_family = AF_INET;
    output_addr.sin_port = htons(OUTPUT_PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &output_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid destination IP\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("=================================\n");
    printf(" UDP REPEATER SIMULATION STARTED\n");
    printf("=================================\n");
    printf("Listening on port: %d\n", INPUT_PORT);
    printf("Forwarding to port: %d\n", OUTPUT_PORT);

    /* Continuously receive and forward datagrams */
    while (1) {

        bytes_received = recvfrom(
            sockfd,
            buffer,
            sizeof(buffer),
            0,
            NULL,
            NULL
        );

        if (bytes_received < 0) {
            perror("Receive failed");
            continue;
        }

        printf("\nReceived %zd bytes\n", bytes_received);

        /* Forward received data */
        bytes_sent = sendto(
            sockfd,
            buffer,
            (size_t)bytes_received,
            0,
            (struct sockaddr *)&output_addr,
            sizeof(output_addr)
        );

        if (bytes_sent < 0) {
            perror("Forwarding failed");
            continue;
        }

        if (bytes_sent != bytes_received) {
            fprintf(stderr, "Incomplete datagram transmission\n");
            continue;
        }

        printf("Forwarded %zd bytes successfully\n", bytes_sent);
    }

    close(sockfd);

    return EXIT_SUCCESS;
}

