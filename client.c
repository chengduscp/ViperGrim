
/*
 A simple client in the internet domain using TCP
 Usage: ./client hostname port (./client 192.168.0.151 10000)
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>      // define structures like hostent
#include <stdlib.h>
#include <strings.h>

void error(char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd; //Socket descriptor
    int portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server; //contains tons of information, including the server's IP address
    char buffer[1024];
    char fileBuf[1024];
    float probCorrupt;
    float probIgnore;
    char* fileName; 
    char ack[] = "ACK";
    char init[] = "INIT";
    int fileIdx;
    int i;
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
  
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); //create a new socket
    if (sockfd < 0) 
        error("ERROR opening socket");
    
    server = gethostbyname(argv[1]); //takes a string like "www.yahoo.com", and returns a struct hostent which contains information, as IP address, address type, the length of the addresses...
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    fileName = argv[3];
    probIgnore = atof(argv[4]);
    probCorrupt = atof(argv[5]);

    if(probIgnore < 0.0 || probIgnore > 1.0)
       probIgnore = 0.0;
      
    if(probCorrupt < 0.0 || probCorrupt > 1.0)
       probCorrupt = 0.0;

    printf("probCorrupt = %f, probIgnore = %f", probCorrupt, probIgnore);
    
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //initialize server's address
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    /*initialize connection */
    sendto(sockfd, init, strlen(init),0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    n = recvfrom(sockfd, buffer, strlen(ack), 0, NULL, NULL);    
    if(n >= 0)
    {
       printf("from server: %s\n", buffer);
    }
    else
       error("ERROR on init");
    memset(buffer,0, 1024);
    
    n = recvfrom(sockfd,buffer,1024, 0, NULL, NULL); //read from the socket
    if (n < 0) 
         error("ERROR reading from socket");
    else
    {
       printf("from server2: %s\n", buffer);
       for(i = 0; i < 1024; i++, fileIdx++)
       {
          fileBuf[fileIdx] = buffer[i];
       }
       sendto(sockfd, ack, strlen(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    }
    
    close(sockfd); //close socket
    
    return 0;
}
