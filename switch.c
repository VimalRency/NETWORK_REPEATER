#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/select.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>

#define MAX_PORTS 2
#define TABLE_SIZE 100
#define BUFFER_SIZE 2048

typedef struct
{
    unsigned char mac[ETH_ALEN];
    int port;
    int valid;
} MACEntry;

static MACEntry mac_table[TABLE_SIZE];

static void print_mac(const unsigned char *mac)
{
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static void learn_mac(const unsigned char *mac, int port)
{
    int free_slot = -1;

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        if (mac_table[i].valid &&
            memcmp(mac_table[i].mac, mac, ETH_ALEN) == 0)
        {
            if (mac_table[i].port != port)
            {
                mac_table[i].port = port;
                printf("[LEARN] MAC moved to port %d: ", port + 1);
                print_mac(mac);
                printf("\n");
            }
            return;
        }

        if (!mac_table[i].valid && free_slot == -1)
        {
            free_slot = i;
        }
    }

    if (free_slot != -1)
    {
        memcpy(mac_table[free_slot].mac, mac, ETH_ALEN);
        mac_table[free_slot].port = port;
        mac_table[free_slot].valid = 1;

        printf("[LEARN] ");
        print_mac(mac);
        printf(" -> Port %d\n", port + 1);
    }
}

static int find_mac(const unsigned char *mac)
{
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        if (mac_table[i].valid &&
            memcmp(mac_table[i].mac, mac, ETH_ALEN) == 0)
        {
            return mac_table[i].port;
        }
    }

    return -1;
}

static int create_socket(const char *interface, int *ifindex)
{
    int fd;
    struct sockaddr_ll address;

    *ifindex = (int)if_nametoindex(interface);

    if (*ifindex == 0)
    {
        perror("Invalid interface");
        return -1;
    }

    fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (fd < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(ETH_P_ALL);
    address.sll_ifindex = *ifindex;

    if (bind(fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind");
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char *argv[])
{
    int fd[MAX_PORTS];
    int ifindex[MAX_PORTS];
    char buffer[BUFFER_SIZE];

    if (argc != 3)
    {
        printf("Usage: sudo %s <port1> <port2>\n", argv[0]);
        return EXIT_FAILURE;
    }

    fd[0] = create_socket(argv[1], &ifindex[0]);
    if (fd[0] < 0)
        return EXIT_FAILURE;

    fd[1] = create_socket(argv[2], &ifindex[1]);
    if (fd[1] < 0)
    {
        close(fd[0]);
        return EXIT_FAILURE;
    }

    printf("Two-port Layer 2 switch started\n");
    printf("Port 1: %s\n", argv[1]);
    printf("Port 2: %s\n", argv[2]);
    printf("Press Ctrl+C to stop\n\n");

    while (1)
    {
        fd_set readfds;
        int maxfd = (fd[0] > fd[1]) ? fd[0] : fd[1];

        FD_ZERO(&readfds);
        FD_SET(fd[0], &readfds);
        FD_SET(fd[1], &readfds);

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            if (errno == EINTR)
                continue;

            perror("select");
            break;
        }

        for (int in = 0; in < MAX_PORTS; in++)
        {
            if (FD_ISSET(fd[in], &readfds))
            {
                struct sockaddr_ll source;
                socklen_t source_len = sizeof(source);

                ssize_t length = recvfrom(
                    fd[in], buffer, sizeof(buffer), 0,
                    (struct sockaddr *)&source, &source_len);

                if (length < ETH_HLEN)
                    continue;

                /* Ignore frames transmitted by this program */
                if (source.sll_pkttype == PACKET_OUTGOING)
                    continue;

                struct ethhdr *eth = (struct ethhdr *)buffer;

                /* Learn the source MAC address */
                learn_mac(eth->h_source, in);

                /* Find the destination port */
                int out = find_mac(eth->h_dest);

                /* Broadcast, multicast or unknown destination */
                if ((eth->h_dest[0] & 1U) != 0U || out == -1)
                {
                    out = 1 - in;
                    printf("[FLOOD] Port %d -> Port %d\n",
                           in + 1, out + 1);
                }
                else if (out == in)
                {
                    printf("[FILTER] Destination is on incoming port\n");
                    continue;
                }
                else
                {
                    printf("[FORWARD] Port %d -> Port %d\n",
                           in + 1, out + 1);
                }

                struct sockaddr_ll destination;
                memset(&destination, 0, sizeof(destination));

                destination.sll_family = AF_PACKET;
                destination.sll_protocol = htons(ETH_P_ALL);
                destination.sll_ifindex = ifindex[out];
                destination.sll_halen = ETH_ALEN;

                memcpy(destination.sll_addr, eth->h_dest, ETH_ALEN);

                if (sendto(fd[out], buffer, (size_t)length, 0,
                           (struct sockaddr *)&destination,
                           sizeof(destination)) < 0)
                {
                    perror("sendto");
                }
            }
        }
    }

    close(fd[0]);
    close(fd[1]);

    return EXIT_SUCCESS;
}
