#ifndef _gbn_h
#define _gbn_h

#include<sys/types.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<signal.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<netinet/in.h>
#include<errno.h>
#include<netdb.h>
#include<time.h>

/*----- Error variables -----*/
extern int h_errno;
extern int errno;

/*------ To address make error -------*/
#define h_addr h_addr_list[0]

/*----- Protocol parameters -----*/
#define LOSS_PROB .09    /* Loss probability                            */
#define CORR_PROB 1e-3    /* Corruption probability                      */
#define DATALEN   1024    /* Length of the payload                       */
#define N         1024    /* Max number of packets a single call to gbn_send can process */
#define TIMEOUT      1    /* Timeout to resend packets (1 second)        */
#define CONN_BROKEN  5    /* Number of timeouts before connection is considered broken   */

/*----- Packet types -----*/
#define SYN      0        /* Opens a connection                          */
#define SYNACK   1        /* Acknowledgement of a SYN packet             */
#define DATA     2        /* Data packets                                */
#define DATAACK  3        /* Acknowledgement of a DATA packet            */
#define FIN      4        /* Ends a connection                           */
#define FINACK   5        /* Acknowledgement of a FIN packet             */
#define RST      6        /* Reset packet used to reject new connections */

/*----- Go-Back-n packet format -----*/
typedef struct {
    uint8_t  type;            /* Packet type (e.g. SYN, DATA, ACK, FIN)     */
    uint8_t  seqnum;          /* Packet sequence number                     */
    uint16_t checksum;        /* Packet checksum                            */
    uint16_t payloadlen;      /* Length of payload                          */
    uint8_t data[DATALEN];    /* Pointer to payload                         */
} __attribute__((packed)) gbnhdr;

/*----- State definitions -----*/
enum states {
    CLOSED,         /* Socket is closed to connections (0)     */
    BOUND,          /* Socket is bound to a port (1)           */
    LISTENING,      /* Socket is listening for connections (2) */
    SYN_SENT,       /* SYN has been sent (3)                   */
    SYN_RCVD,       /* SYN has been received (4)               */
    ESTABLISHED,    /* Socket connection is established (5)    */
    FIN_SENT,       /* FIN has been sent (6)                   */
    FIN_RCVD,       /* FIN has been received (7)               */
    BROKEN          /* Connection is broken (8)                */
};

/*----- Socket identifiers and state info -----*/
typedef struct state_t {
    enum states status;                /* Current state of socket                   */
    int sockfd;                        /* Source socket descriptor/file handler     */
    int seqnum;                        /* Last seqnum to be transmitted succesfully */
    int expectedseqnum;                /* The next seqnum expected                  */
    struct sockaddr *destaddr;         /* Destination socket address                */
    socklen_t destsocklen;             /* Length of destination address             */
} state_t;

/*----- Sequence and window info -----*/
typedef struct window {
    int window;                 /* Window size (N)              */
    volatile int numtimeouts;   /* Number of recorded timeouts  */
} window;

extern state_t s;

void gbn_init();
int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen);
int gbn_listen(int sockfd, int backlog);
int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen);
int gbn_socket(int domain, int type, int protocol);
int gbn_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int gbn_close(int sockfd);
ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t  maybe_recvfrom(int  s, char *buf, size_t len, int flags, \
            struct sockaddr *from, socklen_t *fromlen);
uint16_t checksum(uint16_t *buf, int nwords);

#endif
