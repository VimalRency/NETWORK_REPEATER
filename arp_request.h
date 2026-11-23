/* arp_request.c  - Server A
 * Build:  gcc arp_request.c -o arp_request
 * Run:    sudo ./arp_request eth0 192.168.1.10 192.168.1.200
 *         (interface, my IP, target IP)
 */
#include "arp_common.h"
#include <time.h>

/* ---------- DSA: ARP cache = hash table with chaining ---------- */
#define TABLE_SIZE 16

struct cache_node {
    unsigned char ip[4];
    unsigned char mac[6];
    struct cache_node *next;          /* linked list for collisions */
};

static struct cache_node *table[TABLE_SIZE];

static unsigned int hash_ip(const unsigned char ip[4])
{
    return (ip[0] + ip[1] + ip[2] + ip[3]) % TABLE_SIZE;
}

/* Insert or update an entry */
static void cache_put(const unsigned char ip[4], const unsigned char mac[6])
{
    unsigned int idx = hash_ip(ip);
    struct cache_node *cur = table[idx];

    while (cur) {
        if (memcmp(cur->ip, ip, 4) == 0) {   /* already present: update */
            memcpy(cur->mac, mac, 6);
            return;
        }
        cur = cur->next;
    }
    struct cache_node *n = malloc(sizeof(*n));
    if (!n) { perror("malloc"); exit(1); }
    memcpy(n->ip, ip, 4);
    memcpy(n->mac, mac, 6);
    n->next = table[idx];                    /* insert at head */
    table[idx] = n;
}

/* Lookup: returns node or NULL */
static struct cache_node *cache_get(const unsigned char ip[4])
{
    struct cache_node *cur = table[hash_ip(ip)];
    while (cur) {
        if (memcmp(cur->ip, ip, 4) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

static void cache_print(void)
{
    printf("\n--- ARP cache ---\n");
    for (int i = 0; i < TABLE_SIZE; i++) {
        for (struct cache_node *c = table[i]; c; c = c->next) {
            printf("%u.%u.%u.%u  ->  ", c->ip[0], c->ip[1], c->ip[2], c->ip[3]);
            print_mac(c->mac);
            printf("\n");
        }
    }
}

static void cache_free(void)
{
    for (int i = 0; i < TABLE_SIZE; i++) {
        struct cache_node *c = table[i];
        while (c) { struct cache_node *t = c->next; free(c); c = t; }
        table[i] = NULL;
    }
}

/* ---------- Main program ---------- */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <iface> <my_ip> <target_ip>\n", argv[0]);
        return 1;
    }

    unsigned char my_ip[4], target_ip[4], my_mac[6];
    int ifindex;

    if (inet_pton(AF_INET, argv[2], my_ip) != 1 ||
        inet_pton(AF_INET, argv[3], target_ip) != 1) {
        fprintf(stderr, "Invalid IP address\n");
        return 1;
    }

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sock < 0) { perror("socket (use sudo)"); return 1; }
    if (get_if_info(sock, argv[1], my_mac, &ifindex) < 0) return 1;

    /* 1. Check the cache first */
    struct cache_node *hit = cache_get(target_ip);
    if (hit) {
        printf("Cache hit: ");
        print_mac(hit->mac);
        printf("\n");
        close(sock);
        return 0;
    }
    printf("Cache miss, sending ARP request...\n");

    /* 2. Build the ARP request frame */
    struct eth_arp_frame f;
    memset(&f, 0, sizeof(f));
    memset(f.dst, 0xff, 6);                    /* broadcast */
    memcpy(f.src, my_mac, 6);
    f.type = htons(ETH_P_ARP);

    f.arp.htype = htons(1);
    f.arp.ptype = htons(ETH_P_IP);
    f.arp.hlen  = 6;
    f.arp.plen  = 4;
    f.arp.oper  = htons(ARP_REQUEST);
    memcpy(f.arp.sha, my_mac, 6);
    memcpy(f.arp.spa, my_ip, 4);
    memset(f.arp.tha, 0, 6);                   /* unknown */
    memcpy(f.arp.tpa, target_ip, 4);

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family  = AF_PACKET;
    addr.sll_ifindex = ifindex;
    addr.sll_halen   = 6;
    memset(addr.sll_addr, 0xff, 6);

    if (sendto(sock, &f, sizeof(f), 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("sendto");
        return 1;
    }

    /* 3. Wait up to 3 seconds for the reply */
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    time_t start = time(NULL);
    while (time(NULL) - start < 3) {
        unsigned char buf[2048];
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n < (ssize_t)sizeof(struct eth_arp_frame)) continue;

        struct eth_arp_frame *r = (struct eth_arp_frame *)buf;
        if (ntohs(r->type) != ETH_P_ARP) continue;
        if (ntohs(r->arp.oper) != ARP_REPLY) continue;
        if (memcmp(r->arp.spa, target_ip, 4) != 0) continue;

        printf("ARP reply received: %u.%u.%u.%u is at ",
               r->arp.spa[0], r->arp.spa[1], r->arp.spa[2], r->arp.spa[3]);
        print_mac(r->arp.sha);
        printf("\n");

        cache_put(r->arp.spa, r->arp.sha);     /* store in hash table */
        cache_print();
        cache_free();
        close(sock);
        return 0;
    }

    printf("No ARP reply (timeout)\n");
    cache_free();
    close(sock);
    return 1;
}