#include "gbn.h"

/* Initialize global variable with the state of the client/server socket */
state_t sockstate;

/* Initialize global variable with the client window values */
window windowstate;

/* Return checksum for buf */
uint16_t checksum(uint16_t *buf, int nwords)
{
    uint32_t sum;

    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return ~sum;
}

/* Helper to create packets */
void create_pkt(gbnhdr *packet, int type, int seqnum)
{
    packet->type     = type;
    packet->seqnum   = seqnum;
    packet->checksum = 0;
}

/* Helper tp calculate the checksum */
void calc_checksum(gbnhdr *packet, size_t len)
{
    /* Note: Packet's checksum value is 0 when this is calculated */
    packet->checksum = checksum((uint16_t *)packet, (len / (sizeof(uint16_t))));
}

/* Timeout handler */
void timeouthandler(int signo)
{
    /* Increment the number of recorded timeouts */
    windowstate.numtimeouts += 1;
}

/*-----------------------------------------------------------------------*/

/* Create the socket interface with the given domain, type, and protocol. */
/* Nonblocking                                                            */
int gbn_socket(int domain, int type, int protocol)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    int sockfd;

    /*----- Randomizing the seed. This is used by the rand() function -----*/
    srand((unsigned)time(0));

    if ((sockfd = socket(domain, type, protocol)) == -1){
        fprintf(stderr, "gbn_socket: error opening socket\n");
        perror("gbn_socket");
        return(-1);
    }

    /* Update state */
    sockstate.sockfd = sockfd;
    sockstate.status = CLOSED;
    /* Packet sequence number (0-255) */
    sockstate.seqnum = rand() % 256;
    sockstate.expectedseqnum = sockstate.seqnum;

    /* Update window */
    windowstate.numtimeouts = 0;
    windowstate.window      = 1;

    fprintf(stdout, "gbn_socket: socket created\n");

    return sockfd;
}

/* Set the server socket status to LISTENING. */
/* For this implementation, backlog is 1.     */
/* Nonblocking                                */
int gbn_listen(int sockfd, int backlog)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    if (sockstate.status != BOUND){
        fprintf(stderr, "gbn_listen: server socket can only transition from BOUND to LISTENING\n");
        return(-1);
    }

    /* Update state */
    sockstate.status = LISTENING;

    fprintf(stdout, "gbn_listen: socket is listening\n");

    return(0);
}

/* Associate and reserve a port for use by the server socket. */
/* Nonblocking                                                */
int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    int bindstatus;

    if ((bindstatus = bind(sockfd, server, socklen)) == -1){
        fprintf(stderr, "gbn_bind: error binding server socket\n");
        perror("gbn_bind");
        return(-1);
    }

    /* Update state */
    sockstate.status = BOUND;

    fprintf(stdout, "gbn_bind: socket is bound\n");

    return bindstatus;
}

/* Close the socket. */
/* Blocking          */
int gbn_close(int sockfd)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    int closestatus;              /* Status of close socket function          */
    int bytessent;                /* Number of bytes sent to server           */
    int bytesrec;                 /* Number of bytes received from server     */
    char recbuf[sizeof(gbnhdr)];  /* Buffer for received packets              */

    gbnhdr FINpacket;             /* FIN packet                               */
    gbnhdr *FINACKpacket;         /* Used to cast buffer received from server */

    /* Expected by recvfrom */
    struct sockaddr from;
    socklen_t fromlen = sizeof(from);

    switch(sockstate.status){
        case 0:         /* CLOSED       */
            fprintf(stderr, "gbn_close: socket is already closed\n");
            return(0);
        case 1:         /* BOUND        */
        case 2:         /* LISTENING    */
        case 6:         /* FIN_SENT     */
        case 7:         /* FIN_RCVD     */
        case 8:         /* BROKEN       */
            /* For above cases, there is no current connection - cleanly close */
            if ((closestatus = close(sockfd)) == -1){
                fprintf(stderr, "gbn_close: error closing socket\n");
                perror("gbn_close");
                return(-1);
            }
            /* Update state */
            sockstate.status = CLOSED;
            fprintf(stdout, "gbn_close: socket closed\n");
            return closestatus;
        case 3:         /* SYN_SENT     */
        case 4:         /* SYN_RCVD     */
        case 5:         /* ESTABLISHED  */
            /* Handle these cases below */
            break;
    }

    /* Remaining cases: SYN_SENT, SYN_RCVD, ESTABLISHED */

    fprintf(stdout, "gbn_close: sending FIN\n");

    /* Set final seqnum */
    sockstate.seqnum = sockstate.expectedseqnum;

    /* Create FIN packet */
    memset(&FINpacket, 0, sizeof(gbnhdr));
    create_pkt(&FINpacket, FIN, sockstate.seqnum);
    calc_checksum(&FINpacket, sizeof(gbnhdr));

    fprintf(stdout, "gbn_close: packet type: %d\n", FINpacket.type);
    fprintf(stdout, "gbn_close: packet seqnum: %d\n", FINpacket.seqnum);
    fprintf(stdout, "gbn_close: packet checksum: %d\n", FINpacket.checksum);
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    for(; windowstate.numtimeouts < CONN_BROKEN; ){

        /* Send FIN packet */
        if ((bytessent = sendto(sockfd, (void *)&FINpacket, sizeof(gbnhdr), 0, (const struct sockaddr *)sockstate.destaddr, sockstate.destsocklen)) == -1){
            fprintf(stderr, "gbn_close: error sending FIN packet\n");
            perror("gbn_close");
            return(-1);
        }

        /* Begin timer */
        signal(SIGALRM, timeouthandler);   /* Install the handler */
        alarm(TIMEOUT);                    /* Set the alarm       */

        /* Update state */
        sockstate.status = FIN_SENT;

        fprintf(stdout, "gbn_close: waiting for FINACK...\n");

        /* Block and wait for FINACK */
        if ((bytesrec = maybe_recvfrom(sockfd, recbuf, sizeof(gbnhdr), 0, &from, &fromlen)) == -1){ 
            fprintf(stderr, "gbn_close: error receiving FINACK packet\n");
            
            /* Handle timeout */
            if (errno == EINTR){
                
                /* windowstate.numtimeouts is incremented in the signal handler */
                fprintf(stdout, "gbn_close: timeout waiting for FINACK\n");
                /* Timed-out CONN_BROKEN times */
                if (windowstate.numtimeouts == CONN_BROKEN){
                    sockstate.status = BROKEN;
                    fprintf(stderr, "gbn_close: timed out %d times - connection is broken\n", CONN_BROKEN);
                    return(-1);
                }

            }
        } else {
            /* Reset number of timeouts */
            windowstate.numtimeouts = 0;
            
            /* Turn off the alarm */
            alarm(0);

            /* Cast FINACK packet */
            FINACKpacket = (gbnhdr*) recbuf;

            /* Validate checksum */
            uint16_t recchecksum = FINACKpacket->checksum;
            FINACKpacket->checksum = 0;
            calc_checksum(FINACKpacket, sizeof(gbnhdr));
            if (FINACKpacket->checksum != recchecksum){
                fprintf(stderr, "gbn_close: received corrupted packet - got: %d, expected: %d\n", FINACKpacket->checksum, recchecksum);
                continue;
            }

            /* Validate seqnum */
            if (sockstate.seqnum  != FINACKpacket->seqnum) {
                fprintf(stderr, "gbn_close: received out of order packet - got: %d, expected: %d\n", FINACKpacket->seqnum, sockstate.seqnum);
                continue;
            }

            fprintf(stdout, "gbn_close: client received FINACK\n");
            fprintf(stdout, "gbn_close: type: %d\n", FINACKpacket->type);
            fprintf(stdout, "gbn_close: seqnum: %d\n", FINACKpacket->seqnum);
            fprintf(stdout, "gbn_close: checksum: %d\n", recchecksum);

            break;

        }

    }

    /* Close socket */
    if ((closestatus = close(sockfd)) == -1){
        fprintf(stderr, "gbn_close: error closing socket\n");
        perror("gbn_close");
        return(-1);
    }

    /* Update state */
    sockstate.status = CLOSED;

    return closestatus;
}

/* Send messages between sockets.                       */
/* Returns number of bytes transmitted, or -1 on error. */
/* Blocking                                             */
ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    /* Max number of packets is N */
    int newwindownum;             /* New window size                          */
    int numtosend;                /* Number of packets to send within window  */
    int maxseqnum;                /* Highest ACK sequence number we have seen */
    int lastpacket;               /* Used for conditional                     */
    int extrabytes;               /* Number of payload bytes in final packet  */
    int index;                    /* Index for inner loop                     */
    int packetsACKed;             /* Number of packets ACK'ed from server     */
    int totalpacketstosend;       /* Number of packets that need to be sent   */
    int bytestotal;               /* Total bytes we have sent                 */
    int bytessent;                /* Number of bytes sent to client           */
    int bytesrec;                 /* Number of bytes received from client     */
    char recbuf[sizeof(gbnhdr)];  /* Buffer for received packets              */

    gbnhdr DATApacket;            /* DATA packet                              */
    gbnhdr *DATAACKpacket;        /* Used to cast buffer received from server */

    /* Expected by recvfrom */
    struct sockaddr from;
    socklen_t fromlen = sizeof(from);

    /* Set packet count */
    packetsACKed = 0;

    /* Set conditional */
    lastpacket = 0;

    /* Set total number of packets to send */
    totalpacketstosend = (int) len / DATALEN;
    if (len % DATALEN != 0) {
        totalpacketstosend += 1;
    }

    /* Set extra bytes for final packet */
    extrabytes = len % DATALEN;

    fprintf(stdout, "gbn_send: totalpacketstosend: %d\n", totalpacketstosend);

    /* Set byte count */
    bytestotal = 0;

    /* Set for loop window */
    numtosend = windowstate.window;

    maxseqnum = 0;

    if (sockstate.status == BOUND) {
        perror("gbn_send");
        fprintf(stderr, "gbn_send: cannot send packet from BOUND state\n");
        return(-1);
    }

    if (sockstate.status == BROKEN) {
        perror("gbn_send");
        fprintf(stderr, "gbn_send: cannot send packet from BROKEN state\n");
        return(-1);
    }

    while(packetsACKed < totalpacketstosend){
        
        fprintf(stdout, "gbd_send: sending packet in window %d\n", windowstate.window);

        /* Set index */
        index = 0;

        /* Iterate over our transmission window */
        for (; index < numtosend; index++) {

            /* Create DATA packet */
            memset(&DATApacket, 0, sizeof(gbnhdr));

            create_pkt(&DATApacket, DATA, sockstate.seqnum);

            if ( (packetsACKed + index + (windowstate.window - numtosend)) == totalpacketstosend - 1) {
                fprintf(stdout, "gbd_send: final packet payload size is: %d\n", extrabytes);
                DATApacket.payloadlen = extrabytes;  
                /* We have the final packet */
                lastpacket = 1;
            } else {
                DATApacket.payloadlen = DATALEN;                
            }
            
            /* Add buf to packet */
            fprintf(stdout, "index: %d\n", index);
            fprintf(stdout, "packetsACKed: %d\n", packetsACKed);
            fprintf(stdout, "windowstate.window - numtosend: %d\n", (windowstate.window - numtosend));
            memcpy(DATApacket.data, buf + ( (index * DATALEN) + (packetsACKed * DATALEN) + ( (windowstate.window - numtosend) * DATALEN) ), DATApacket.payloadlen);
            
            /* Calculate the checksum */
            calc_checksum(&DATApacket, sizeof(gbnhdr));

            fprintf(stdout, "\n" );
            fprintf(stdout, "\n" );
            fprintf(stdout, "------------------------------------------\n");
            fprintf(stdout, "gbd_send: packet type: %d\n", DATApacket.type);
            fprintf(stdout, "gbd_send: packet seqnum: %d\n", DATApacket.seqnum);
            fprintf(stdout, "gbd_send: packet checksum: %d\n", DATApacket.checksum);
            fprintf(stdout, "gbd_send: packet data: %s\n", DATApacket.data);
            fprintf(stdout, "------------------------------------------\n");
            fprintf(stdout, "\n" );
            fprintf(stdout, "\n" );

            /* Begin timer */
            signal(SIGALRM, timeouthandler);   /* Install the handler */
            alarm(TIMEOUT);                    /* Set the alarm       */

            /* Send DATA packet */
            if ((bytessent = sendto(sockfd, (void *)&DATApacket, sizeof(gbnhdr), flags, (const struct sockaddr *)sockstate.destaddr, sockstate.destsocklen)) == -1){
                fprintf(stderr, "gbn_send: error sending DATA packet\n");
                perror("gbn_send");
                return(-1);
            }

            /* Increment sequence number */
            sockstate.seqnum = ((sockstate.seqnum + 1) % 256);

            if(lastpacket) {
                fprintf(stdout, "gbn_send: sent final DATA packet\n");
                break;
            }

        }

        fprintf(stdout, "gbn_send: waiting for DATAACK...\n");


        /* Decrease sliding window range */
        numtosend = 0;

        /* Block and wait for DATAACK */
        if ((bytesrec = maybe_recvfrom(sockfd, recbuf, sizeof(gbnhdr), flags, &from, &fromlen)) == -1){
            fprintf(stderr, "gbn_send: error receiving DATAACK packet\n");
            
            /* Handle timeout */
            if (errno == EINTR){

                /* windowstate.numtimeouts is incremented in the signal handler */
                fprintf(stdout, "gbn_send: timeout waiting for DATAACK\n");
                /* Timed-out CONN_BROKEN times */
                if (windowstate.numtimeouts == CONN_BROKEN){
                    sockstate.status = BROKEN;
                    fprintf(stderr, "gbn_send: client has timed out %d times - connection is broken\n", CONN_BROKEN);
                    return(-1);
                }
                /* Update window */
                windowstate.window = 1;
                numtosend = windowstate.window;

                /* Update sequence number */
                sockstate.seqnum = sockstate.expectedseqnum;

                fprintf(stdout, "gbn_send: window changed to: %d\n", windowstate.window);
                continue;
            }
        } else {
            /* Reset number of timeouts */
            windowstate.numtimeouts = 0;
            
            /* Turn off the alarm */
            alarm(0);

            /* Cast DATAACK packet */
            DATAACKpacket = (gbnhdr*) recbuf;

            /* Validate checksum */
            uint16_t recchecksum = DATAACKpacket->checksum;
            DATAACKpacket->checksum = 0;
    
            /* Calculate the checksum to compare the two */
            calc_checksum(DATAACKpacket, sizeof(gbnhdr));
            if (DATAACKpacket->checksum != recchecksum){
                fprintf(stderr, "gbn_send: received corrupted packet - got: %d, expected: %d\n", DATAACKpacket->checksum, recchecksum);
                /* Update sequence number */
                sockstate.seqnum = sockstate.expectedseqnum;
                windowstate.window = 1;
                numtosend = windowstate.window;
                continue;
            }

            /* Validate seqnum */
            if (DATAACKpacket->seqnum != sockstate.expectedseqnum) {
                fprintf(stderr, "gbn_send: received out of order packet - expected seqnum: %d, DATAACKpacket seqnum: %d\n", sockstate.expectedseqnum, DATAACKpacket->seqnum);
                if (DATAACKpacket->seqnum > maxseqnum) {
                    int numpacketsdiff = DATAACKpacket->seqnum - maxseqnum;
                    maxseqnum = DATAACKpacket->seqnum;
                    packetsACKed += numpacketsdiff;
                }
                /* Update sequence numbers          */
                /* Receiver sends LAST KNOWN seqnum */
                sockstate.expectedseqnum = ((DATAACKpacket->seqnum + 1) % 256);
                fprintf(stderr, "gbn_send: updating seqnum from: %d to: %d\n", sockstate.seqnum, sockstate.expectedseqnum);
                sockstate.seqnum = sockstate.expectedseqnum;
                windowstate.window = 1;
                numtosend = windowstate.window;
                continue;
            }

            fprintf(stdout, "\n");
            fprintf(stdout, "\n");
            fprintf(stdout, "------------------------------------------\n");
            fprintf(stdout, "gbn_send: client received DATAACK\n");
            fprintf(stdout, "gbn_send: type: %d\n", DATAACKpacket->type);
            fprintf(stdout, "gbn_send: seqnum: %d\n", DATAACKpacket->seqnum);
            fprintf(stdout, "gbn_send: checksum: %d\n", recchecksum);
            fprintf(stdout, "------------------------------------------\n");
            fprintf(stdout, "\n");
            fprintf(stdout, "\n");

            /* Update window */
            switch(windowstate.window){
                case(1):
                case(2):
                    newwindownum = (windowstate.window * 2);
                case(4):
                    break;
            }

            /* Calc how many new packets we can send */
            if (windowstate.window != newwindownum) {
                if (newwindownum == 4) {
                    numtosend = newwindownum - 1;
                } else {
                    numtosend = newwindownum;
                }
                windowstate.window = newwindownum;
            } else {
                numtosend++;
            }

            fprintf(stdout, "gbn_send: window changed to: %d\n", windowstate.window);

            maxseqnum = DATAACKpacket->seqnum;

            /* Update sequence number */
            sockstate.expectedseqnum = ((DATAACKpacket->seqnum + 1) % 256);

            /* Update packets ACK'ed */
            packetsACKed++;

            fprintf(stdout, "gbn_send: packages ACKed: %d\n", packetsACKed);

            /* Update bytes count */
            bytestotal += bytessent;

        }

    }

    return bytestotal;
}

/* Receive messages from one socket to another.      */
/* Returns number of bytes recieved, or -1 on error. */
/* Blocking                                          */
ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    gbnhdr *DATApacket;           /* Used to cast buffer received from client */
    gbnhdr ACKpacket;             /* ACK packet                               */

    int bytessent;                /* Number of bytes sent to client           */
    int bytesrec;                 /* Number of bytes received from client     */
    char recbuf[sizeof(gbnhdr)];  /* Buffer for received packets              */

    int rectype;                  /* Received packet type                     */

    /* Expected by recvfrom */
    struct sockaddr from;
    socklen_t fromlen = sizeof(from);

    /* Flag denoting error in transmission */
    int needpacket = 1;

    if (sockstate.status == FIN_RCVD) {
        fprintf(stderr, "gbn_recv: socket can only receive in the ESTABLISHED state\n");
        return 0;
    }

    if (sockstate.status != ESTABLISHED){
        fprintf(stderr, "gbn_recv: socket can only receive in the ESTABLISHED state\n");
        return(-1);
    }

    fprintf(stdout, "gbn_recv: waiting for packets...\n");

    while(needpacket) {
        
        /* Block and wait for connection from the client */
         if ((bytesrec = maybe_recvfrom(sockfd, recbuf, sizeof(gbnhdr), flags, &from, &fromlen)) == -1){ 
            fprintf(stderr, "gbn_recv: error receiving packet from client\n");
            return(-1);
        }

        needpacket = 0;

        /* Cast DATA packet */
        DATApacket = (gbnhdr*) recbuf;

        /* Validate checksum */
        uint16_t recchecksum = DATApacket->checksum;
        DATApacket->checksum = 0;
        
        /* Calculate the checksum to compare the two */
        calc_checksum(DATApacket, sizeof(gbnhdr));
        if (DATApacket->checksum != recchecksum){
            fprintf(stderr, "gbn_recv: received corrupted packet - expected sum: %d, recchecksum: %d\n", DATApacket->checksum, recchecksum);
            needpacket = 1;
        }

        fprintf(stdout, "\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "------------------------------------------\n");
        fprintf(stdout, "gbn_recv: server received packet\n");
        fprintf(stdout, "gbn_recv: packet type: %d\n", DATApacket->type);
        fprintf(stdout, "gbn_recv: packet seqnum: %d\n", DATApacket->seqnum);
        fprintf(stdout, "gbn_recv: packet checksum: %d\n", recchecksum);
        fprintf(stdout, "gbn_recv: packet data: %s\n", DATApacket->data);
        fprintf(stdout, "------------------------------------------\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "\n");

        /* Validate seqnum */
        if (DATApacket->seqnum != sockstate.expectedseqnum) {
            fprintf(stderr, "gbn_recv: received out of order packet - expected seqnum: %d, DATApacket seqnum: %d\n", sockstate.expectedseqnum, DATApacket->seqnum);
            needpacket = 1;
        }

        uint8_t ACKtype = DATAACK;
        uint8_t ACKseqnum = DATApacket->seqnum;

        switch(DATApacket->type){
            case 2:     /* DATA */
                /* ACK packet defaults to DATAACK */
                rectype = 2;
                break;
            case 4:     /* FIN  */
                /* Set ACK packet to FIN_RCVD     */
                rectype = 4;
                ACKtype = 7;
                break;
        }

        /* If there were no errors i.e. checksum and seqnum are good          */
        /* Then we want to increment seqnum, since we have an accepted packet */
        /* Otherwise, by not incrementing we reject the packet                */
        if (!needpacket) {
            /* Only write DATA packets */
            if (DATApacket->type != 4) {
                /* If we haven't already seen the file */
                if (DATApacket->seqnum != sockstate.seqnum) {
                    /* Save data to file */
                    memcpy(buf, DATApacket->data, DATApacket->payloadlen);
                }
            }
            /* Store seqnum */
            sockstate.seqnum          = DATApacket->seqnum;
            sockstate.expectedseqnum  = ((sockstate.seqnum + 1) % 256);
        } else {
            /* Not a valid packet, so we set the seqnum to the last good seqnum we have */
            ACKseqnum = ((sockstate.expectedseqnum - 1) % 256);
        }

        /* Create ACK packet */
        memset(&ACKpacket, 0, sizeof(ACKpacket));
        create_pkt(&ACKpacket, ACKtype, ACKseqnum);
        calc_checksum(&ACKpacket, sizeof(gbnhdr));

        fprintf(stdout, "\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "------------------------------------------\n");
        fprintf(stdout, "gbn_recv: packet type: %d\n", ACKpacket.type);
        fprintf(stdout, "gbn_recv: packet seqnum: %d\n", ACKpacket.seqnum);
        fprintf(stdout, "gbn_recv: packet checksum: %d\n", ACKpacket.checksum);
        fprintf(stdout, "------------------------------------------\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "\n");

        /* Send ACK packet unreliably */
        if ((bytessent = sendto(sockfd, (void *)&ACKpacket, sizeof(gbnhdr), flags, (const struct sockaddr *)sockstate.destaddr, sockstate.destsocklen)) == -1){
            fprintf(stderr, "gbn_recv: error sending ACK packet to client\n"); 
            perror("gbn_recv");
            return(-1);
        }

        fprintf(stdout, "gbn_recv: server sent ACK\n\n");

        if (!needpacket) {
            switch(rectype){
                case 2:     /* Received DATA */
                    return DATApacket->payloadlen;
                case 4:     /* Received FIN  */
                    sockstate.status = FIN_RCVD;
                    return(0);
            }
        }
    }

    fprintf(stderr, "gbn_recv: error receiving packet from client\n");
    return(-1);

}

/* Connect the client socket to the server socket.                                        */
/* Client will send a SYN and then wait for a reply. If a SYNACK is not received before   */
/* the timeout, the client will resend the SYN. Since this is only a two-way handshake,   */
/* it is possible that the server will send a SYNACK that is not delivered successfully   */
/* to the client. In this case, the client will timeout 5 times and assume the connection */
/* is broken.                                                                             */
/* Blocking.                                                                              */
int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    int bytessent;                /* Number of bytes sent to server           */
    int bytesrec;                 /* Number of bytes received from server     */
    char recbuf[sizeof(gbnhdr)];  /* Buffer for received packets              */

    gbnhdr SYNpacket;             /* SYN packet                               */
    gbnhdr *SYNACKpacket;         /* Used to cast buffer received from server */

    /* Expected by recvfrom */
    struct sockaddr from;
    socklen_t fromlen = sizeof(from);

    fprintf(stdout, "gbn_connect: client sending SYN\n");

    if (sockstate.status != CLOSED){
        fprintf(stderr, "gbn_connect: client socket can only establish a connection from the CLOSED state\n");
        return(-1);
    }

    /* Save server info */
    sockstate.destaddr = (struct sockaddr *)server;
    sockstate.destsocklen = socklen;

    /* Create SYN packet */
    memset(&SYNpacket, 0, sizeof(gbnhdr));
    create_pkt(&SYNpacket, SYN, sockstate.seqnum);
    calc_checksum(&SYNpacket, sizeof(gbnhdr));

    fprintf(stdout, "gbn_connect: packet type: %d\n", SYNpacket.type);
    fprintf(stdout, "gbn_connect: packet seqnum: %d\n", SYNpacket.seqnum);
    fprintf(stdout, "gbn_connect: packet checksum: %d\n", SYNpacket.checksum);

    /* Timeout up to CONN_BROKEN times on startup */
    for(; windowstate.numtimeouts < CONN_BROKEN; ){

        /* Send SYN packet */
        if ((bytessent = sendto(sockfd, (void *)&SYNpacket, sizeof(gbnhdr), 0, (const struct sockaddr *)sockstate.destaddr, sockstate.destsocklen)) == -1){
            fprintf(stderr, "gbn_connect: error sending SYN packet\n");
            perror("gbn_connect");
            return(-1);
        }

        /* Begin timer */
        signal(SIGALRM, timeouthandler);   /* Install the handler */
        alarm(TIMEOUT);                    /* Set the alarm       */

        /* Update state */
        sockstate.status = SYN_SENT;

        fprintf(stdout, "gbn_connect: waiting for SYNACK...\n");

        /* Block and wait for SYNACK */
        if ((bytesrec = maybe_recvfrom(sockfd, recbuf, sizeof(gbnhdr), 0, &from, &fromlen)) == -1){ 
            fprintf(stderr, "gbn_connect: error receiving SYNACK packet\n");
            
            /* Handle timeout */
            if (errno == EINTR){
                /* windowstate.numtimeouts is incremented in the signal handler */
                fprintf(stdout, "gbn_connect: timeout waiting for SYNACK\n");
                /* Timed-out CONN_BROKEN times */
                if (windowstate.numtimeouts == CONN_BROKEN){
                    sockstate.status = BROKEN;
                    fprintf(stderr, "gbn_connect: client has timed out %d times - connection is broken\n", CONN_BROKEN);
                    return(-1);
                }
            }
        } else {
            /* Reset number of timeouts */
            windowstate.numtimeouts = 0;
            /* Turn off the alarm */
            alarm(0);
            break;
        }

    }

    /* Cast SYNACK packet */
    SYNACKpacket = (gbnhdr*) recbuf;

    /* Validate checksum */
    uint16_t recchecksum = SYNACKpacket->checksum;
    SYNACKpacket->checksum = 0;
    calc_checksum(SYNACKpacket, sizeof(gbnhdr));
    if (SYNACKpacket->checksum != recchecksum){
        fprintf(stderr, "gbn_connect: received corrupted packet - got: %d, expected: %d\n", SYNACKpacket->checksum, recchecksum);
        return(-1);
    }

    /* Validate seqnum */
    if (sockstate.seqnum  != SYNACKpacket->seqnum) {
        fprintf(stderr, "gbn_connect: received out of order packet - got: %d, expected: %d\n", SYNACKpacket->seqnum, sockstate.seqnum);
        return(-1);
    }

    fprintf(stdout, "gbn_connect: client received SYNACK\n");
    fprintf(stdout, "gbn_connect: type: %d\n", SYNACKpacket->type);
    fprintf(stdout, "gbn_connect: seqnum: %d\n", SYNACKpacket->seqnum);
    fprintf(stdout, "gbn_connect: checksum: %d\n", recchecksum);

    /* Update sequence number */
    sockstate.seqnum = ((sockstate.seqnum + 1) % 256);
    sockstate.expectedseqnum = sockstate.seqnum;

    /* Update state */
    sockstate.status = ESTABLISHED;

    return(0);
}

/* Accept a connection from the client to the server.                                      */
/* Server will wait to receive a SYN from the client. Once a valid SYN packet is received, */
/* the server will send a SYNACK. Since this is only a two-way handshake, the "accept"     */
/* function completes once the SYNACK is in flight, and the status is set to SYN_RCVD.     */
/* The SYNACK control packet is sent unreliably.                                           */
/* When the server receives the first DATA packet, the status is set to ESTABLISHED.       */
/* Blocking.                                                                               */
int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen)
{
    fprintf(stdout, "\n");
    fprintf(stdout, "\n");

    gbnhdr *SYNpacket;            /* Used to cast buffer received from client */
    gbnhdr SYNACKpacket;          /* SYNACK packet                            */

    int bytessent;                /* Number of bytes sent to client           */
    int bytesrec;                 /* Number of bytes received from client     */
    char recbuf[sizeof(gbnhdr)];  /* Buffer for received packets              */

    int clientsockfd;             /* Client socket file descriptor            */

    if (sockstate.status != LISTENING){
        fprintf(stderr, "gbn_accept: server socket can only transition from LISTENING to SYN_RCVD\n");
        return(-1);
    }

    fprintf(stdout, "gbn_accept: server waiting for client...\n");

    while(1) {
        /* Block and wait for connection from the client */
        if ((bytesrec = maybe_recvfrom(sockfd, recbuf, sizeof(gbnhdr), 0, client, socklen)) == -1){ 
            fprintf(stderr, "gbn_accept: error receiving SYN packet from client\n");
            continue;
        }

        /* Cast SYN packet */
        SYNpacket = (gbnhdr*) recbuf;

        /* Validate checksum */
        uint16_t recchecksum = SYNpacket->checksum;
        SYNpacket->checksum = 0;
        calc_checksum(SYNpacket, sizeof(gbnhdr));
        if (SYNpacket->checksum != recchecksum){
            fprintf(stderr, "gbn_accept: received corrupted packet - got: %d, expected: %d\n", SYNpacket->checksum, recchecksum);
             continue;
        }

        fprintf(stdout, "gbn_accept: server received SYN\n");
        fprintf(stdout, "gbn_accept: packet type: %d\n", SYNpacket->type);
        fprintf(stdout, "gbn_accept: packet seqnum: %d\n", SYNpacket->seqnum);
        fprintf(stdout, "gbn_accept: packet checksum: %d\n", recchecksum);

        break;

    }

    /* Store seqnum */
    sockstate.seqnum         = SYNpacket->seqnum;
    sockstate.expectedseqnum = ((sockstate.seqnum + 1) % 256);

    /* Create a socket with the client */
    if ((clientsockfd = socket(client->sa_family, SOCK_DGRAM, 0)) == -1){
        fprintf(stderr, "gbn_accept: error creating client socket\n");
        return(-1);
    }

    fprintf(stdout, "gbn_accept: server connected to client\n");

    /* Save client info */
    sockstate.destaddr = client;
    sockstate.destsocklen = *socklen;

    /* Set server socket state */
    sockstate.status = SYN_RCVD;

    /* Create SYNACK packet */
    memset(&SYNACKpacket, 0, sizeof(gbnhdr));
    create_pkt(&SYNACKpacket, SYNACK, sockstate.seqnum);
    calc_checksum(&SYNACKpacket, sizeof(gbnhdr));

    fprintf(stdout, "gbn_accept: server sending SYNACK\n");
    fprintf(stdout, "gbn_accept: packet type: %d\n", SYNACKpacket.type);
    fprintf(stdout, "gbn_accept: packet seqnum: %d\n", SYNACKpacket.seqnum);
    fprintf(stdout, "gbn_accept: packet checksum: %d\n", SYNACKpacket.checksum);

    /* Send SYNACK packet unreliably */
    if ((bytessent = sendto(sockfd, (void *)&SYNACKpacket, sizeof(gbnhdr), 0, (const struct sockaddr *)sockstate.destaddr, sockstate.destsocklen)) == -1){
        fprintf(stderr, "gbn_accept: error sending SYNACK packet to client\n"); 
        perror("gbn_accept");
        return(-1);
    }

    fprintf(stdout, "gbn_accept: server sent SYNACK\n");

    /* Send SYNACK over UDP - expect ESTABLISHED connection */
    sockstate.status = ESTABLISHED;

    return sockfd;
}

/* Simulate recvfrom functionality in an unreliable environment */
ssize_t maybe_recvfrom(int  s, char *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    /*----- Packet not lost -----*/
    if (rand() > LOSS_PROB*RAND_MAX){

        /*----- Receiving the packet -----*/
        int retval = recvfrom(s, buf, len, flags, from, fromlen);

        /*----- Packet corrupted -----*/
        if (rand() < CORR_PROB*RAND_MAX){
            /*----- Selecting a random byte inside the packet -----*/
            int index = (int)((len-1)*rand()/(RAND_MAX + 1.0));

            /*----- Inverting a bit -----*/
            char c = buf[index];
            if (c & 0x01)
                c &= 0xFE;
            else
                c |= 0x01;
            buf[index] = c;
        }

        return retval;

    }

    /*----- Packet lost -----*/
    return(len);  /* Simulate a success */

}
