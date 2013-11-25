
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
#define BUFF_SIZE 1024
#define FILE_INTERVAL 1000
#include <sys/types.h>
#include <ctype.h>

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


  timeout.tv_sec = 6;
  timeout.tv_usec = 0;
  if(argc < 2)
    fprintf(stderr, "ERROR, no port provided\n");

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
  
  cwndPack = (cwnd+999) / 1000;
  lastPackSize = cwnd%1000 == 0 ? 1000 : cwnd % 1000;
  printf("DEBUG: OPen file\n");
  //if(rdt.newsockfd < 0)
    //error("ERROR on accept");
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

  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  printf("DEBUG readCwndBytes\n");
  readCwndBytes(&rdt, cwndPack, lastPackSize);
  rdt.sendNWin.start = 0;
  rdt.sendNWin.end   = n-1;
  for(i = 0 ; i < rdt.sendNWin.n ; i++)
  {

    srand(time(NULL));
    randNo = rand() % 100;
    success = ((float)randNo)/100;
    if(success > probIgnore && success > probCorrupt)
    {
      writeBytes = sendto(rdt.sockfd, rdt.sendNWin.packets[i].buf, 1000, 0, (struct sockaddr *)&rdt.client, rdt.clientLen);
      if(writeBytes < 0)
        error("ERROR on WRITE");
    }
    else
    {

      debug_head = getHeader(rdt.sendNWin.packets[i].buf, 1000);
      printf("fail to send packet %d\n",debug_head->seq);
    }
    
  }
  printf("DEBUG sending packets\n");

  do
  {
    
    printf("top\n");
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
      timeout.tv_sec += 8;
      FD_SET(rdt.sockfd, &write_set);
    }
    if(FD_ISSET(rdt.sockfd, &read_set))
    {
      printf("reading\n");
      rcvBytes =   recvfrom(rdt.sockfd, rdt.initBuf, rdt.initBufSize, 0,
                           (struct sockaddr *)&rdt.client, &rdt.clientLen);
      if(rdt.recieve_header->type == ACK &&
         rdt.sendNWin.window[(lastAcked+1)%rdt.sendNWin.n] == rdt.recieve_header->ack)
      {
        if(rdt.fileSize > 0)
          FD_SET(rdt.sockfd, &write_set);
        printf("ACK: %d\n", rdt.recieve_header->ack);
        lastAcked = lastAcked++; 
        lastAcked = lastAcked % rdt.sendNWin.n;
        rdt.sendNWin.start = (rdt.sendNWin.start+1)%rdt.sendNWin.n;
        //not sure if this is where i should sent end
        rdt.sendNWin.end = (rdt.sendNWin.end+1)%rdt.sendNWin.n;
        timeOfUpdate = time(NULL);
        seconds = difftime(timeOfUpdate, timeOfSelect);
        sec_int = seconds;
        //timeout.tv_sec += 6;
        if(timeout_flag == 1)
        {
          //timeout.tv_sec += 8;
          timeout_flag = 0;
          printf("timeout_flag = %d\n", timeout_flag);
        }
      }
    }
    if(FD_ISSET(rdt.sockfd, &write_set))
    {
      printf("writing\n");
      if(timeout_flag == 1)
      {
        printf("sending N packets\n");
        for(i = 0; i < rdt.sendNWin.n; i++)
        {
           
          srand(time(NULL));
          randNo = rand() % 100;
          success = ((float)randNo)/100;

          if(success > probIgnore && success > probCorrupt)
          {

            writeBytes = sendto(rdt.sockfd, rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf,
                                1000,0,(struct sockaddr *)&rdt.client, rdt.clientLen);
            debug_head = getHeader(rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf, 1000);
            printf("resending packet %d\n", debug_head->seq);
            sleep(1);
            if(writeBytes < 0)
              error("ERROR on WRITE");
          }
          else
          {
            debug_head = getHeader(rdt.sendNWin.packets[(rdt.sendNWin.start+i)%rdt.sendNWin.n].buf, 1000);
            printf("fail to send packet %d\n",debug_head->seq);
            sleep(1);
          }
        }

        FD_CLR(rdt.sockfd, &write_set);
      }
      if(rdt.fileSize > 0 && timeout_flag == 0)
      {
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
        printf("rdt.fileIdx = %d\n", rdt.fileIdx);

        srand(time(NULL));
        randNo = rand() % 100;
        success = ((float)randNo)/100;

        if(success > probIgnore && success > probCorrupt)
        {

          writeBytes = sendto(rdt.sockfd, rdt.sendNWin.packets[sendIdx].buf,
                              1000,0,(struct sockaddr *)&rdt.client, rdt.clientLen);
          if(writeBytes < 0)
            error("ERROR on WRITE");
        }
        else
        {
          printf("fail to send packet %d\n", rdt.send_header->seq);
        }

        FD_CLR(rdt.sockfd, &write_set);
        sleep(1);
      }
//      if(timeout_flag == 1)
//        timeout_flag = 0;
    }

    
    #if 0
    /*if(fileIdx == rdt.recieve_header->ack)*/
    //for now not sure what to do for the else condition
    interval = min(tempSize, (FILE_INTERVAL-sizeof(packet_header_t)));
    fileIdx += interval;
    bytesRead = fread(rdt.send_payload, 1, interval,rdt.f);
    n = -1;
    /*swap seq and ack of recieved ack*/
    temp = rdt.recieve_header->seq;
    rdt.send_header->seq = rdt.recieve_header->ack;
    rdt.send_header->ack = temp;
 
    while(n < 0)
    {
      srand(time(NULL));
      randNo = rand() % 100;
      printf("randNo = %d\n", randNo);
      success = ((float)randNo)/100;
      printf("success = %f\n", success);
      if(success > probIgnore && success > probCorrupt)
      {
        writeBytes = sendto(rdt.sockfd, rdt.fBuf, interval+sizeof(packet_header_t), 0, (struct sockaddr *)&rdt.client, rdt.clientLen);
        if(writeBytes < 0)
          error("ERROR on WRITE");
      }
      else
      {
        printf("fail to send\n");
      }
    
      if(setsockopt(rdt.sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        error("ERROR SETTING TIMEOUT");

      n = recvfrom(rdt.sockfd, rdt.initBuf, rdt.initBufSize, 0, (struct sockaddr *)&rdt.client, &rdt.clientLen);
      if(!(n >= 0 && rdt.recieve_header->type == ACK))
      {
        printf("timeout\n");
      }
    }
    tempSize -= interval;
#endif
  }while(rdt.recieve_header->ack < size);


  rdt.send_header->type = FIN;
  rdt.send_header->ack = 0;
  rdt.send_header->seq = 0;
  rdt.send_header->len = 0;
  rdt.send_header->cwnd = 0;
  sendto(rdt.sockfd, rdt.send_header, sizeof(packet_header_t), 0, (struct sockaddr*)&rdt.client, rdt.clientLen);

  close(rdt.sockfd);

  return 0;

}
