
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
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>

// Constants
#define TIMEOUT_INTERVAL 6
#define BUFF_SIZE 1024
#define FILE_INTERVAL 1000

void error(char *msg)
{
  perror(msg);
  exit(1);
}

int timeout_flag = 0;
void signalHandler(int type)
{
    printf("HI at alarm\n");
    timeout_flag = 1;
}

static int inline min(int a, int b)
{
  if(a < b)
    return a;
  else
    return b;
}

static int inline max(int a, int b)
{
  if(a > b)
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
  int fileIdx;
  int fileSize;
  FILE *f;

  // Sending headers and payload pointers
  packet_header_t *send_header;
  char *send_payload;

  packet_header_t *recieve_header;

  // Random number generator
  int rand_seed;

  // Buffers
  char *fBuf, *initBuf;
  int fBufSize, initBufSize;

  //Go-Back-N Windows 
  sendNWin_t sendNWin;

} rdt_t;

void initializeRDT(rdt_t *rdt, char *fBuf, char *initBuf, int fBufSize, int initBufSize) {
  memset(rdt, sizeof(rdt_t), 0);
  rdt->fBuf = &fBuf[0];
  rdt->initBuf = &initBuf[0];
  rdt->recieve_header = (packet_header_t *)&initBuf[0];
  rdt->fBufSize = fBufSize;
  rdt->initBufSize = initBufSize;
  memset(fBuf, 0, fBufSize);
  rdt->send_header = (packet_header_t *)&fBuf[0]; // header always points to the beginning of the data packet
  rdt->send_payload = getPayload(fBuf, fBufSize);
  rdt->rand_seed = 0;
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

void readCwndBytes(rdt_t* rdt, int n,int lastPackSize )
{
  int i;
  int interval;
  int bytesRead;
  rdt->fileIdx = 0;
  rdt->sendNWin.packets = (packet_t *)malloc(n * sizeof(packet_t));
  rdt->sendNWin.window = (int *)malloc(n *sizeof(int));

  memset((void *)rdt->sendNWin.packets, 0, n*sizeof(packet_t));
  memset((void *)rdt->sendNWin.window, 0, n*sizeof(int));

  rdt->sendNWin.n = n;
  for(i = 0; i < n - 1; i++)
  {
    rdt->sendNWin.packets[i].size = 1000;
  }
  rdt->sendNWin.packets[n-1].size = lastPackSize;
   
  for(i = 0; i < n ; i++)
  {
    rdt->send_header = getHeader(rdt->sendNWin.packets[i].buf, 1000);
    rdt->send_header->type = DATA;
    rdt->send_header->seq = rdt->fileIdx;
    interval = min( rdt->fileSize, rdt->sendNWin.packets[i].size -sizeof(packet_header_t));

    rdt->sendNWin.window[i] = rdt->fileIdx+interval;
    bytesRead = fread(rdt->sendNWin.packets[i].buf+sizeof(packet_header_t), 1, interval,rdt->f);
    rdt->sendNWin.packets[i].actualSize = interval+sizeof(packet_header_t);
    rdt->send_header->len = interval;
    rdt->fileSize = rdt->fileSize - interval;
    rdt->fileIdx += interval;
    if( rdt->fileSize == 0)
      break;
  }

}

int main(int argc, char *argv[])
{
  // int end_server = 0;
  int i, randNo;
  long size;
  struct stat st;
  char fBuf[FILE_INTERVAL];
  int interval, bytesRead, writeBytes, rcvBytes, n;
  char ackBuf[4];
  char initBuf[1000];
  struct timeval timeout;
  float probIgnore, probCorrupt, success;
  int tempSize, temp;
  int fileIdx;
  int cwnd, cwndPack, lastPackSize;
  int maxfdp;
  int result;
  int lastAcked = -1;
  int sendIdx;
  double seconds;
  int sec_int;
  time_t timeOfSelect, timeOfUpdate;
  rdt_t rdt;
  packet_header_t* debug_head;
  fd_set read_set, write_set;


  timeout.tv_sec = TIMEOUT_INTERVAL;
  timeout.tv_usec = 0;
  if(argc < 2) {
    fprintf(stderr, "ERROR, no port provided\n");
    fprintf(stderr, "Usage: server.exe port cwnd probIgnore probCorrupt\n");
    return;
  }

  signal(SIGALRM, signalHandler);

  // Initialize RDT data structure variables
  initializeRDT(&rdt, fBuf, initBuf, sizeof(fBuf), sizeof(initBuf));
  
  cwnd = atoi(argv[2]);
  probIgnore = atof(argv[3]);
  probCorrupt = atof(argv[4]);


  if(probIgnore < 0.0 || probIgnore > 1.0)
    probIgnore = 0.0;

  if(probCorrupt < 0.0 || probCorrupt > 1.0)
    probCorrupt = 0.0;
  /* initialize UDP socket */
  getInitialUDPSocket(&rdt, atoi(argv[1]));

  getInitialClientPacket(&rdt);

  // Get window of rdt object ready
  cwndPack = (cwnd+999) / 1000;
  lastPackSize = cwnd%1000 == 0 ? 1000 : cwnd % 1000;

  // Get the file the client sent
  printf("Opening file...\n");
  rdt.f = fopen(rdt.fileName, "rb");
  if(!rdt.f)
  {
    rdt.send_header->type = NO_SUCH_FILE;
    rdt.send_header->ack = 0;
    rdt.send_header->seq = 0;
    rdt.send_header->len = 0;
    rdt.send_header->cwnd = 0;
    sendto(rdt.sockfd, rdt.send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&rdt.client, rdt.clientLen);
    error("ERROR could not find file");
  }
  stat(rdt.fileName, &st);
  size = (long) st.st_size;
  rdt.fileSize = size;

  // Get the connection open
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  readCwndBytes(&rdt, cwndPack, lastPackSize);
  rdt.sendNWin.start = 0;
  rdt.sendNWin.end   = n-1;
  srand(rdt.rand_seed + time(NULL));
  rdt.rand_seed += rand() % 256;
  for(i = 0 ; i < rdt.sendNWin.n ; i++)
  {
    randNo = rand() % 100;
    success = ((float)randNo)/100;
    rdt.send_header = getHeader(rdt.sendNWin.packets[i].buf, 1000);
    if (success > probCorrupt)
    {
      rdt.send_header->checksum = 0;
    }
    else
    {
      rdt.send_header->checksum = 1;
    }
    writeBytes = sendto(rdt.sockfd, rdt.sendNWin.packets[i].buf, 1000, 0, (struct sockaddr *)&rdt.client, rdt.clientLen);
    if(writeBytes < 0)
      error("ERROR on WRITE");
    
  }
  printf("Sending packets...\n");

  do
  {
    printf("\nNext packet round...\n");
    FD_SET(rdt.sockfd, &read_set);
    timeOfSelect = time(NULL);
    result = select(rdt.sockfd+1, &read_set, &write_set, NULL, &timeout); 
    if(result  < 0)
    {
      error("SELECT");
    }
    if(result == 0)
    {
      printf("TIMEOUT\n");
      timeout_flag = 1;
      timeout.tv_sec = TIMEOUT_INTERVAL;
      FD_SET(rdt.sockfd, &write_set);
    }
    if(FD_ISSET(rdt.sockfd, &read_set))
    {
      srand(rdt.rand_seed + time(NULL));
      rdt.rand_seed += rand() % 256;
      randNo = rand() % 100;
      success = ((float)randNo)/100;
      printf("Ignore: %.2f vs %.2f\n", success, probIgnore);

      if(success > probIgnore)
      {
        printf("Reading ACK...\n");
        rcvBytes =   recvfrom(rdt.sockfd, rdt.initBuf, rdt.initBufSize, 0,
                             (struct sockaddr *)&rdt.client, &rdt.clientLen);
        if(rdt.recieve_header->type == ACK &&
           rdt.sendNWin.window[(lastAcked+1)%rdt.sendNWin.n] == rdt.recieve_header->ack)
        {
          if(rdt.fileSize > 0)
            FD_SET(rdt.sockfd, &write_set);
          printf("ACK: %d\n", rdt.recieve_header->ack);
          lastAcked++;
          // Get the appropriate packets
          rdt.sendNWin.start = (rdt.sendNWin.start+1)%rdt.sendNWin.n;
          rdt.sendNWin.end = (rdt.sendNWin.end+1)%rdt.sendNWin.n;
          timeOfUpdate = time(NULL);
          seconds = difftime(timeOfUpdate, timeOfSelect);
          sec_int = seconds;
          if(timeout_flag == 1)
          {
            timeout_flag = 0;
          }
        }
        else
        {
          debug_head = getHeader(rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf, 1000);
          printf("Fail to recieve packet %d\n",debug_head->seq);
          sleep(1);
        }
      }
    }
    if(FD_ISSET(rdt.sockfd, &write_set))
    {
      printf("Writing packet...\n");
      if(timeout_flag == 1)
      {
        srand(rdt.rand_seed + time(NULL));
        rdt.rand_seed += rand() % 256;
        printf("Sending N packets because of timeout...\n");
        for(i = 0; i < rdt.sendNWin.n; i++)
        {
          randNo = rand() % 100;
          success = ((float)randNo)/100;
          printf("Corruption: %.2f vs %.2f\n", success, probIgnore);
          rdt.send_header = getHeader(rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf, 1000);
          if (success > probCorrupt)
          {
            rdt.send_header->checksum = 0;
          }
          else
          {
            rdt.send_header->checksum = 1;
            printf("Sending corrupted packet...\n");
          }
          writeBytes = sendto(rdt.sockfd, rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf,
                              1000,0,(struct sockaddr *)&rdt.client, rdt.clientLen);
          debug_head = getHeader(rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf, 1000);
          printf("Resending packet %d...\n", debug_head->seq);
          if(writeBytes < 0)
            error("ERROR on WRITE");
        }

        FD_CLR(rdt.sockfd, &write_set);
      }
      if(rdt.fileSize > 0 && timeout_flag == 0)
      {
        // Get next packet
        sendIdx = lastAcked;
        sendIdx = sendIdx % rdt.sendNWin.n;
        rdt.send_header = getHeader(rdt.sendNWin.packets[sendIdx].buf, 1000);
        rdt.send_header->type = DATA;
        rdt.send_header->seq = rdt.fileIdx;
        interval = min( rdt.fileSize, rdt.sendNWin.packets[sendIdx].size -sizeof(packet_header_t));
  
        rdt.sendNWin.packets[sendIdx].actualSize = interval+sizeof(packet_header_t);
        rdt.send_header->len = interval;
        rdt.sendNWin.window[sendIdx] = rdt.fileIdx+interval;
        bytesRead = fread(rdt.sendNWin.packets[sendIdx].buf+sizeof(packet_header_t),
                          1, interval,rdt.f);
        rdt.fileSize = rdt.fileSize - interval;
        rdt.fileIdx += interval;

        // Generate random number
        srand(rdt.rand_seed + time(NULL));
        rdt.rand_seed += rand() % 256;
        randNo = rand() % 100;
        success = ((float)randNo)/100;
        printf("Corrupt: %.2f vs %.2f\n", success, probCorrupt);

        // Check for success of sending packet
        rdt.send_header = getHeader(rdt.sendNWin.packets[sendIdx].buf, 1000);
        if(success > probCorrupt)
        {
          rdt.send_header->checksum = 0;
        }
        else
        {
          rdt.send_header->checksum = 1;
          printf("Sending corrupted packet...\n");
        }
        writeBytes = sendto(rdt.sockfd, rdt.sendNWin.packets[sendIdx].buf,
                            1000,0,(struct sockaddr *)&rdt.client, rdt.clientLen);
        if(writeBytes < 0)
          error("ERROR on WRITE");

        // Get ready for next packet
        FD_CLR(rdt.sockfd, &write_set);
      }
    }
  } while(rdt.recieve_header->ack < size);

  // Finish sending the packet
  rdt.send_header->type = FIN;
  rdt.send_header->ack = 0;
  rdt.send_header->seq = 0;
  rdt.send_header->len = 0;
  rdt.send_header->cwnd = 0;
  sendto(rdt.sockfd, rdt.send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&rdt.client, rdt.clientLen);

  close(rdt.sockfd);

  return 0;

}
