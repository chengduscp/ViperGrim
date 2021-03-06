
/*
 A simple client in the internet domain using TCP
 Usage: ./client hostname port probLoss probCorrupt (./client 192.168.0.151 10000 0.0 0.0)
 */
#include "packet_header.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>      // define structures like hostent
#include <stdlib.h>
#include <strings.h>

const char OUTPUT_FILE_NAME[] = "out";

void error(char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd; //Socket descriptor
    int portno, n;
    FILE * output_file;
    struct sockaddr_in serv_addr;
    struct hostent *server; //contains tons of information, including the server's IP address
    char send_buffer[1000];
    char buffer[1000];
    int rand_seed;
    float success;
    float probCorrupt;
    float probLoss;
    char* fileName; 
    packet_header_t *recieve_header;
    char *recieve_payload;
    packet_header_t *send_header;
    char *send_payload;
    char ack[] = "ACK";
    char init[] = "INIT";
    int fileIdx;
    int i;
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port filename probLoss probCorrupt\n", argv[0]);
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
    probLoss = atof(argv[4]);
    probCorrupt = atof(argv[5]);

    /* Create the output file */
    output_file = fopen(OUTPUT_FILE_NAME, "w+");
    if (output_file == 0)
      error("ERROR opening output file");

    if(probLoss < 0.0 || probLoss > 1.0)
       probLoss = 0.0;
      
    if(probCorrupt < 0.0 || probCorrupt > 1.0)
       probCorrupt = 0.0;

    printf("probCorrupt = %f, probLoss = %f", probCorrupt, probLoss);

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //initialize server's address
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    /*initialize connection */
    memset(send_buffer, 0, sizeof(send_buffer));
    send_header = (packet_header_t *)&send_buffer[0]; // header always points to the beginning of the data packet
    send_payload = getPayload(send_buffer, sizeof(send_buffer));
    send_header->type = INIT;
    send_header->seq = 0;
    send_header->ack = 0;
    send_header->cwnd = CWND_DEFAULT;
    send_header->len = strlen(fileName);
    memcpy(send_payload, fileName, send_header->len);
    sendto(sockfd, send_buffer, sizeof(packet_header_t) + send_header->len, 
           0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    /* Start recieving data */
    rand_seed = 0; // For dropping packets
    memset(buffer, 0, sizeof(buffer));
    recieve_header = (packet_header_t *)&buffer[0]; // header always points to the beginning of the data packet
    recieve_payload = getPayload(buffer, sizeof(buffer));

    n = recvfrom(sockfd, buffer, strlen(ack), 0, NULL, NULL);    
    if(n < 0)
       error("ERROR on init");

    if(recieve_header->type != ACK)
      error("ERROR did not get ACK");

    memset(buffer,0, 1000);
    while(1)
    {
      printf("\nRecieve Next header...\n");
      n = recvfrom(sockfd,buffer,1000, 0, NULL, NULL); //read from the socket
      if(recieve_header->type == FIN)
      {
        sleep(2);
        break;
      }

      printf("recieve seq = %d, send ack = %d\n", recieve_header->seq, send_header->ack);
      if (n < 0) 
           error("ERROR reading from socket");
      else if (recieve_header->checksum == 1)
      {
        printf("Corrupted packet %d...\n", recieve_header->seq);
        continue;
      }
      // if seq corresponds with previous ack
      else if(recieve_header->seq == send_header->ack) 
      {
        fwrite(recieve_payload, 1, recieve_header->len, output_file);
        send_header->type = ACK;
        send_header->ack = recieve_header->seq + recieve_header->len;
        send_header->seq = recieve_header->ack;

        // Check if we need to create a corrupted ACK
        srand(rand_seed + time(NULL));
        rand_seed += rand() % 256;
        success = ((rand() % 100) / 100.f);
        if (success > probCorrupt)
        {
          printf("Sending ACK %d...\n", send_header->ack);
          send_header->checksum = 0;
        }
        else
        {
          printf("Sending corrupted ACK %d...\n", send_header->ack);
          send_header->checksum = 1;
        }
        success = ((rand() % 100) / 100.f);
        printf("Loss: %.2f vs %.2f\n", success, probLoss);
        if (success < probLoss) {
          printf("Dropped ACK %d...\n", send_header->ack);
          continue;
        }

        sendto(sockfd, send_buffer, sizeof(send_buffer), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
      }
      else
      {
        srand(rand_seed + time(NULL));
        rand_seed += rand() % 256;

        success = ((rand() % 100) / 100.f);
        if (success > probCorrupt)
        {
          printf("Sending ACK %d...\n", send_header->ack);
          send_header->checksum = 0;
        }
        else
        {
          printf("Sending corrupted ACK %d...\n", send_header->ack);
          send_header->checksum = 1;
        }
        
        success = ((rand() % 100) / 100.f);
        printf("Loss: %.2f vs %.2f\n", success, probLoss);
        if (success < probLoss) {
          printf("Dropped Resending ACK %d...\n", send_header->ack);
          continue;
        }

        printf("Resending ACK %d...\n", send_header->ack);
        sendto(sockfd, send_buffer, sizeof(send_buffer), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
      }
    }
    close(sockfd); //close socket
    
    return 0;
}
