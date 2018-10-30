#include "gbn.h"

int main(int argc, char *argv[]){
	int sockfd;              /* Socket file descriptor of the client        	*/
	int numRead;			 /* Number of packets read 							*/
	socklen_t socklen;	     /* Length of the socket structure sockaddr     	*/
	char buf[DATALEN * N];   /* Buffer for sending packets (1024 * 1024) 	    */
	struct hostent *he;	 	 /* Structure for resolving names into IP addresses */
	FILE *inputFile;     	 /* Input file pointer                              */
	struct sockaddr_in server;

	socklen = sizeof(struct sockaddr);

	/*----- Checking arguments -----*/
	if (argc != 4){
		fprintf(stderr, "usage: sender <hostname> <port> <filename>\n");
		exit(-1);
	}

	/*----- Opening the input file -----*/
	if ((inputFile = fopen(argv[3], "rb")) == NULL){
		perror("fopen");
		exit(-1);
	}

	/*----- Resolving hostname to the respective IP address -----*/
	if ((he = gethostbyname(argv[1])) == NULL){
		perror("gethostbyname");
		exit(-1);
	}

	/*----- Opening the socket -----*/
	/* Family: AF_INET       */
	/* Type: SOCK_DGRAM      */
	/* Protocal: IPPROTO_UDP */
	if ((sockfd = gbn_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
		perror("gbn_socket");
		exit(-1);
	}

	/*--- Setting the server's parameters -----*/
	memset(&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_addr   = *(struct in_addr *)he->h_addr;
	server.sin_port   = htons(atoi(argv[2]));

	/*----- Connecting to the server -----*/
	if (gbn_connect(sockfd, (struct sockaddr *)&server, socklen) == -1){
		perror("gbn_connect");
		exit(-1);
	}

	/*----- Reading from the file and sending it through the socket -----*/
	while ((numRead = fread(buf, 1, DATALEN * N, inputFile)) > 0){
		if (gbn_send(sockfd, buf, numRead, 0) == -1){
			perror("gbn_send");
			exit(-1);
		}
	}

	/*----- Closing the socket -----*/
	if (gbn_close(sockfd) == -1){
		perror("gbn_close");
		exit(-1);
	}

	/*----- Closing the file -----*/
	if (fclose(inputFile) == EOF){
		perror("fclose");
		exit(-1);
	}

	return(0);

}
