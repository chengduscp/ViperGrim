
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
/* this file reads the filecontents and stores it in fileBuf*/
static int inline getContents(char* filename, int* fSize, int socket)
{
  
  FILE *f = fopen(filename, "rb");
  int tempSize = *fSize;
  int readSize;
  int bytesRead;
  int interval;
  int writeBytes;
  char fBuf[FILE_INTERVAL];
  if(f)
  {
    while(tempSize > 0)
    {
      interval  = min(tempSize, FILE_INTERVAL);
      bytesRead  = fread(fBuf, 1, interval,f);
      writeBytes = send(socket, fBuf, interval, 0);
      if(writeBytes < 0)
        error("Writing to socket");
      tempSize  -= interval;
    }
    /*if(fileBuf)
    {
      *fSize = fread(fileBuf, 1, *fSize, f);
    }*/
    fclose(f);
    return 1;
  }
  else
    return 0;
}

int main(int argc, char *argv[])
{
  int sockfd, newsockfd, portno, pid, rc, maxfd;
  struct sockaddr_in server, client;
  socklen_t clientLen;  
  int end_server = 0;
  int i, randNo;
  long size;
  struct stat st;
  char *fileName;
  FILE *f;
  char fBuf[FILE_INTERVAL];
  int interval, bytesRead, writeBytes, n;
  char ackBuf[4];
  char initBuf[1024];

  packet_header_t *recieve_header;
  char *recieve_payload;
  packet_header_t *send_header;
  char *send_payload;

  char ack[] = "ACK";
  struct timeval timeout;
  float probIgnore, probCorrupt, success;
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;
  if(argc < 2)
  {
    fprintf(stderr, "ERROR, no port provided\n");
  }
    
  clientLen = sizeof(client);
  /* initialize UDP socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sockfd < 0)
    error("SOCKET");
  
  memset((char *) &server, 0, sizeof(server));
  portno = atoi(argv[1]);
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port=htons(portno);

  probIgnore = atof(argv[2]);
  probCorrupt = atof(argv[3]);

  if(probIgnore < 0.0 || probIgnore > 1.0)
    probIgnore = 0.0;
    
  if(probCorrupt < 0.0 || probCorrupt > 1.0)
    probCorrupt = 0.0;

  if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    error("BIND ERROR");

  // Read from connection
  memset(fBuf, 0, sizeof(fBuf));
  send_header = (packet_header_t *)&fBuf[0]; // header always points to the beginning of the data packet
  send_payload = getPayload(fBuf, sizeof(fBuf));

  memset(initBuf, 0, sizeof(initBuf));
  recieve_header = (packet_header_t *)&initBuf[0]; // header always points to the beginning of the data packet
  recieve_payload = getPayload(initBuf, sizeof(initBuf));


  n = recvfrom(sockfd, initBuf, sizeof(initBuf), 0, (struct sockaddr*)&client, &clientLen);
  if(n < 0)
    error("ERROR on read");

  // Check if valid header
  if (recieve_header->type != INIT) {
    // recieve_payload
    error("ERROR not valid init");
  }
  printf("Got here 2 %d\n", n);
  printHeader(recieve_header);
  fileName = (char *)malloc(recieve_header->len + 1);
  printf("Got here 1 %p %d\n", fileName, recieve_header->len);
  memcpy(fileName, recieve_payload, recieve_header->len);
  printf("Got here 0\n");
  printf("from client: %s\n", recieve_payload);

  // We don't have to worry about security, so we just pick 0 as our starting point
  send_header->type = ACK;
  send_header->ack = 0;
  send_header->seq = 0;
  send_header->len = 0;
  send_header->cwnd = recieve_header->cwnd;

  sendto(sockfd, send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&client, clientLen);

  if(newsockfd < 0)
    error("ERROR on accept");
  f = fopen(fileName, "rb");
  if(!f)
  {
    send_header->type = NO_SUCH_FILE;
    send_header->ack = 0;
    send_header->seq = 0;
    send_header->len = 0;
    send_header->cwnd = 0;
    sendto(sockfd, send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&client, clientLen);
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
      writeBytes = sendto(sockfd, fBuf, interval, 0, (struct sockaddr *)&client, clientLen);
      if(writeBytes < 0)
        error("ERROR on WRITE");
    }
    else
    {
      printf("fail to send\n");
    }
  
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
      error("ERROR SETTING TIMEOUT");

    n = recvfrom(sockfd, ackBuf, 4, 0, (struct sockaddr *)&client, &clientLen);
    if(n >= 0)
    {
      printf("from client2: %s\n", ackBuf);
    }
    else
    {
      printf("timeout\n");
    }
  }
  close(sockfd);

  return 0;

}
