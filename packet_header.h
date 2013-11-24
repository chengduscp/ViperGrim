#ifndef PACKET_HEADER_H
#define PACKET_HEADER_H

#include <stdint.h>

#define PACKET_INTERVAL 1000


typedef enum header_type_t {
  INIT, // Initialize connection
  FIN, // Finish connection
  ACK, // Acknowledgement
  DATA, // Regular packet
  NO_SUCH_FILE, // File does not exist
  UNKNOWN // Unknown type
} header_type_t;


#define CWND_DEFAULT 10

typedef struct packet_header_t {
  header_type_t type;
  uint32_t seq; // Sequence number
  uint32_t ack; // Acknowledge number
  uint32_t cwnd; // Current window size
  uint32_t len; // Length of the data
  uint32_t checksum; // Check corruption of data
} packet_header_t;

typedef struct packet_t {
  char buf[1000];
  int fileLoc;
  int size;
  int actualSize;

} packet_t;

typedef struct sendWin_t {
  packet_t* packets;
  int n;
  int start;
  int end;
  
} sendNWin_t;
packet_header_t *getHeader(char *data, uint32_t size) {
  if(size < sizeof(packet_header_t))
    return 0;
  return (packet_header_t *)data;
}



char *getPayload(char *data, uint32_t size) {
  if(size < sizeof(packet_header_t))
    return 0;
  return (data + sizeof(packet_header_t));
}

void printHeader(packet_header_t *h) {
  printf("Header:\n");
  switch(h->type) {
    case INIT:
      printf("INIT\n");
      break;
    case FIN:
      printf("FIN\n");
      break;
    case ACK:
      printf("ACK\n");
      break;
    case DATA:
      printf("DATA\n");
      break;
    case NO_SUCH_FILE:
      printf("NO_SUCH_FILE\n");
      break;
    case UNKNOWN:
      printf("UNKNOWN\n");
      break;
  }
  printf("seq: %u, ack: %u, cwnd: %u, len: %u, checksum: %u\n",
    h->seq, h->ack, h->cwnd, h->len, h->checksum);
}

#endif
