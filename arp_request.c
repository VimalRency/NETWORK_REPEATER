#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <linux/if_packet.h>

#define RETRIES 3
#define TIMEOUT_MS 2000

struct arp_pkt {
    uint16_t htype, ptype;
    uint8_t hlen, plen;
    uint16_t oper;
    uint8_t sha[6], spa[4], tha[6], tpa[4];
} __attribute__((packed));

static void print_mac(const uint8_t *m) {
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           m[0],m[1],m[2],m[3],m[4],m[5]);
}

static int get_info(const char *name, int *idx, uint8_t mac[6],
                    struct in_addr *ip) {
    int s;
    struct ifreq r;
    if (strlen(name) >= IFNAMSIZ) {
        fprintf(stderr, "Interface name too long\n"); return -1;
    }
    *idx = (int)if_nametoindex(name);
    if (*idx == 0) { fprintf(stderr,"Interface %s not found\n",name); return -1; }
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    memset(&r,0,sizeof(r));
    (void)snprintf(r.ifr_name,sizeof(r.ifr_name),"%s",name);
    if (ioctl(s,SIOCGIFHWADDR,&r)<0) { perror("SIOCGIFHWADDR"); close(s); return -1; }
    memcpy(mac,r.ifr_hwaddr.sa_data,6);
    if (ioctl(s,SIOCGIFADDR,&r)<0) {
        fprintf(stderr,"Interface %s has no IPv4 address\n",name);
        close(s); return -1;
    }
    *ip=((struct sockaddr_in *)&r.ifr_addr)->sin_addr;
    close(s); return 0;
}

static int send_request(int fd,int idx,const uint8_t mac[6],
                        struct in_addr src,struct in_addr dst) {
    uint8_t frame[ETH_HLEN + sizeof(struct arp_pkt)] = {0};
    uint8_t bcast[6]={255,255,255,255,255,255};
    struct ethhdr *eth=(struct ethhdr *)frame;
    struct arp_pkt *a=(struct arp_pkt *)(frame+ETH_HLEN);
    struct sockaddr_ll to;
    memcpy(eth->h_dest,bcast,6); memcpy(eth->h_source,mac,6);
    eth->h_proto=htons(ETH_P_ARP);
    a->htype=htons(1); a->ptype=htons(ETH_P_IP);
    a->hlen=6; a->plen=4; a->oper=htons(1);
    memcpy(a->sha,mac,6); memcpy(a->spa,&src.s_addr,4);
    memset(a->tha,0,6); memcpy(a->tpa,&dst.s_addr,4);
    memset(&to,0,sizeof(to)); to.sll_family=AF_PACKET;
    to.sll_ifindex=idx; to.sll_protocol=htons(ETH_P_ARP);
    to.sll_halen=6; memcpy(to.sll_addr,bcast,6);
    if (sendto(fd,frame,sizeof(frame),0,(struct sockaddr *)&to,sizeof(to))<0) {
        perror("sendto"); return -1;
    }
    return 0;
}

/* 1=matching reply, 0=timeout, -1=socket/system error */
static int wait_reply(int fd,const uint8_t localmac[6],
                      struct in_addr localip,struct in_addr target,
                      int timeout,uint8_t found[6]) {
    struct timespec start,now;
    struct pollfd p={.fd=fd,.events=POLLIN};
    if (clock_gettime(CLOCK_MONOTONIC,&start)<0) { perror("clock_gettime"); return -1; }
    for (;;) {
        long elapsed,remain;
        uint8_t buf[2048];
        ssize_t n;
        struct ethhdr eth;
        struct arp_pkt a;
        int pr;
        if (clock_gettime(CLOCK_MONOTONIC,&now)<0) { perror("clock_gettime"); return -1; }
        elapsed=(now.tv_sec-start.tv_sec)*1000L+
                (now.tv_nsec-start.tv_nsec)/1000000L;
        remain=(long)timeout-elapsed;
        if (remain<=0) return 0;
        pr=poll(&p,1,(int)remain);
        if (pr==0) return 0;
        if (pr<0) { if(errno==EINTR) continue; perror("poll"); return -1; }
        if (p.revents&(POLLERR|POLLHUP|POLLNVAL)) {
            fprintf(stderr,"Socket/interface poll error\n"); return -1;
        }
        n=recv(fd,buf,sizeof(buf),0);
        if(n<0) { if(errno==EINTR) continue; perror("recv"); return -1; }
        if((size_t)n<ETH_HLEN+sizeof(a)) continue;
        memcpy(&eth,buf,sizeof(eth));
        if(ntohs(eth.h_proto)!=ETH_P_ARP) continue;
        memcpy(&a,buf+ETH_HLEN,sizeof(a));
        if(ntohs(a.htype)!=1 || ntohs(a.ptype)!=ETH_P_IP ||
           a.hlen!=6 || a.plen!=4 || ntohs(a.oper)!=2) continue;
        if(memcmp(a.spa,&target.s_addr,4)!=0 ||
           memcmp(a.tpa,&localip.s_addr,4)!=0 ||
           memcmp(a.tha,localmac,6)!=0) continue;
        memcpy(found,a.sha,6); return 1;
    }
}

int main(int argc,char **argv) {
    int idx,fd;
    uint8_t mac[6],found[6];
    struct in_addr src,dst;
    struct sockaddr_ll bindto;
    if(argc!=3) {
        fprintf(stderr,"Usage: sudo %s <interface> <target-ip>\n",argv[0]);
        return EXIT_FAILURE;
    }
    if(inet_pton(AF_INET,argv[2],&dst)!=1) {
        fprintf(stderr,"Invalid IPv4 target: %s\n",argv[2]); return EXIT_FAILURE;
    }
    if(get_info(argv[1],&idx,mac,&src)<0) return EXIT_FAILURE;
    if(src.s_addr==dst.s_addr) {
        fprintf(stderr,"Target IP is this interface's own IP\n"); return EXIT_FAILURE;
    }
    fd=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ARP));
    if(fd<0) { perror("socket(AF_PACKET)"); fprintf(stderr,"Try sudo\n"); return EXIT_FAILURE; }
    memset(&bindto,0,sizeof(bindto));
    bindto.sll_family=AF_PACKET; bindto.sll_protocol=htons(ETH_P_ARP);
    bindto.sll_ifindex=idx;
    if(bind(fd,(struct sockaddr *)&bindto,sizeof(bindto))<0) {
        perror("bind"); close(fd); return EXIT_FAILURE;
    }
    printf("Interface: %s | Local IP: %s | Local MAC: ",
           argv[1],inet_ntoa(src)); print_mac(mac);
    printf("\nTarget IP: %s\n",argv[2]);
    for(int attempt=1;attempt<=RETRIES;attempt++) {
        int r;
        printf("\n[Attempt %d/%d] Who has %s? Tell %s\n",
               attempt,RETRIES,argv[2],inet_ntoa(src));
        if(send_request(fd,idx,mac,src,dst)<0) { close(fd); return EXIT_FAILURE; }
        r=wait_reply(fd,mac,src,dst,TIMEOUT_MS,found);
        if(r==1) {
            printf("[ARP REPLY] %s is at ",argv[2]); print_mac(found);
            printf("\n[ SUCCESS ] ARP resolution completed\n");
            close(fd); return EXIT_SUCCESS;
        }
        if(r<0) { close(fd); return EXIT_FAILURE; }
        printf("[TIMEOUT] No matching reply within %d ms\n",TIMEOUT_MS);
        if(attempt<RETRIES) printf("[RETRY] Sending request again\n");
    }
    printf("\n[FAILED] No ARP reply after %d attempts.\n",RETRIES);
    printf("Destination MAC remains unresolved; no IP packet is forwarded by this program.\n");
    close(fd); return EXIT_FAILURE;
}
