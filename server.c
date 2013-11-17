
/*Michael Chan CS118 Project 1*/
#include "packet_header.h"

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define BUFF_SIZE 1024
#define FILE_INTERVAL 1024
#include <sys/types.h>
#include <ctype.h>

void error(char *msg)
{
  perror(msg);
  exit(1);
}
static int inline min(int a, int b)
{
  if(a < b)
    return a;
  else
    return b;
}

typedef struct rdt_t {
  // Client/Server addresses
  struct sockaddr_in server, client;
  socklen_t clientLen;

  // File stuff
  int sockfd, newsockfd;
  char *fileName;

  // Sending headers and payload pointers
  packet_header_t *send_header;
  char *send_payload;

  // Buffers
  char *fBuf, *initBuf;
  int fBufSize, initBufSize;
} rdt_t;

void initializeRDT(rdt_t *rdt, char *fBuf, char *initBuf, int fBufSize, int initBufSize) {
  memset(rdt, sizeof(rdt_t), 0);
  rdt->fBuf = &fBuf[0];
  rdt->initBuf = &initBuf[0];
  rdt->fBufSize = fBufSize;
  rdt->initBufSize = initBufSize;
  memset(fBuf, 0, fBufSize);
  rdt->send_header = (packet_header_t *)&fBuf[0]; // header always points to the beginning of the data packet
  rdt->send_payload = getPayload(fBuf, fBufSize);
}

void getInitialUDPSocket(rdt_t *rdt, int portno) {
  rdt->clientLen = sizeof(rdt->client);
  rdt->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(rdt->sockfd < 0)
    error("SOCKET");
  
  memset((char *) &rdt->server, 0, sizeof(rdt->server));
  rdt->server.sin_family = AF_INET;
  rdt->server.sin_addr.s_addr = INADDR_ANY;
  rdt->server.sin_port=htons(portno);

  if (bind(rdt->sockfd, (struct sockaddr *)&rdt->server, sizeof(rdt->server)) < 0)
    error("BIND ERROR");
}

// Get initial connection
void getInitialClientPacket(rdt_t *rdt) {
  int n;
  packet_header_t *recieve_header;
  char *recieve_payload;

  // Read from connection
  memset(rdt->initBuf, 0, sizeof(rdt->initBuf));
  recieve_header = (packet_header_t *)&rdt->initBuf[0]; // header always points to the beginning of the data packet
  recieve_payload = getPayload(rdt->initBuf, rdt->initBufSize);

  n = recvfrom(rdt->sockfd, rdt->initBuf, rdt->initBufSize, 0, (struct sockaddr*)&rdt->client, &rdt->clientLen);
  if(n < 0)
    error("ERROR on read");

  // Check if valid header
  if (recieve_header->type != INIT) {
    // recieve_payload
    error("ERROR not valid init");
  }

  printHeader(recieve_header);
  rdt->fileName = (char *)malloc(recieve_header->len + 1);
  memcpy(rdt->fileName, recieve_payload, recieve_header->len);
  printf("from client: %s\n", recieve_payload);

  // Acknowledge INIT (say we have made connection)  
  // We don't have to worry about security, so we just pick 0 as our starting point
  rdt->send_header->type = ACK;
  rdt->send_header->ack = 0;
  rdt->send_header->seq = 0;
  rdt->send_header->len = 0;
  rdt->send_header->cwnd = recieve_header->cwnd;

  sendto(rdt->sockfd, rdt->send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&rdt->client, rdt->clientLen);
}


int main(int argc, char *argv[])
{
  // int end_server = 0;
  int i, randNo;
  long size;
  struct stat st;
  FILE *f;
  char fBuf[FILE_INTERVAL];
  int interval, bytesRead, writeBytes, n;
  char ackBuf[4];
  char initBuf[1024];
  struct timeval timeout;
  float probIgnore, probCorrupt, success;
  rdt_t rdt;

  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;
  if(argc < 2)
    fprintf(stderr, "ERROR, no port provided\n");

  // Initialize RDT data structure variables
  initializeRDT(&rdt, fBuf, initBuf, sizeof(fBuf), sizeof(initBuf));

  probIgnore = atof(argv[2]);
  probCorrupt = atof(argv[3]);

  if(probIgnore < 0.0 || probIgnore > 1.0)
    probIgnore = 0.0;
    
  if(probCorrupt < 0.0 || probCorrupt > 1.0)
    probCorrupt = 0.0;

  /* initialize UDP socket */
  getInitialUDPSocket(&rdt, atoi(argv[1]));

  getInitialClientPacket(&rdt);

  if(rdt.newsockfd < 0)
    error("ERROR on accept");
  f = fopen(rdt.fileName, "rb");
  if(!f)
  {
    rdt.send_header->type = NO_SUCH_FILE;
    rdt.send_header->ack = 0;
    rdt.send_header->seq = 0;
    rdt.send_header->len = 0;
    rdt.send_header->cwnd = 0;
    sendto(rdt.sockfd, rdt.send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&rdt.client, rdt.clientLen);
    error("ERROR could not find file");
  }


  stat("sample.html", &st);
  size = (long) st.st_size;
  interval = min(size, FILE_INTERVAL);
  bytesRead = fread(fBuf, 1, interval,f);
  n = -1;

  while(n < 0)
  {
    srand(time(NULL));
    randNo = rand() % 100;
    printf("randNo = %d\n", randNo);
    success = ((float)randNo)/100;
    printf("success = %f\n", success);
    if(success > probIgnore && success > probCorrupt)
    {
      writeBytes = sendto(rdt.sockfd, rdt.fBuf, interval, 0, (struct sockaddr *)&rdt.client, rdt.clientLen);
      if(writeBytes < 0)
        error("ERROR on WRITE");
    }
    else
    {
      printf("fail to send\n");
    }
  
    if(setsockopt(rdt.sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
      error("ERROR SETTING TIMEOUT");

    n = recvfrom(rdt.sockfd, ackBuf, 4, 0, (struct sockaddr *)&rdt.client, &rdt.clientLen);
    if(n >= 0)
    {
      printf("from client2: %s\n", ackBuf);
    }
    else
    {
      printf("timeout\n");
    }
  }
  close(rdt.sockfd);

  return 0;

}
