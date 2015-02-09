#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <cerrno>
using namespace std;

const int MAX_MAP_SIZE = 8388608;   // 8 M = 8388608 B
const int MAX_PAYLOAD_SIZE = 8192;
const int SEND_PACKET_SIZE = MAX_PAYLOAD_SIZE + 8;
const int RECV_PACKET_SIZE = 2;

unsigned int DFTPHash(const char *str, unsigned long len) {
  unsigned int seed = 131;
  unsigned int hash = 0;
  
  for (unsigned long i = 0; i < len; ++i) {
    hash = hash * seed + (*str++);
  }
  
  return hash;
}

int main(int argc, char** argv) {
  /********************** parse argument *************************/
  int oc;                     // option character -r or -f
  char* conn_info = NULL;     // option string <recv host>:<recv port>
  char* file_name = NULL;     // option string <filename>
  
  opterr = 0;                 // stop getopt() to output error info
  while ((oc = getopt(argc, argv, "r:f:")) != -1) {
    switch (oc) {
      case 'r':
        conn_info = optarg;
        break;
      case 'f':
        file_name = optarg;
        break;
      default:
        fprintf(stderr, "Illegal Argument!\n");
        return -1;
    }
  }
  
  /********************** init socket *************************/
  char host_name[20];
  int port;
  if (sscanf(conn_info, "%[^:]:%d", host_name, &port) != 2) {
    fprintf(stderr, "Illegal Argument!\n");
    return -1;
  }
  
  int sockfd;
  if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("opening UDP socket!");
    return -1;
  }
  
  /* set timeout = 20 ms */
  timeval tv;
  memset(&tv, 0, sizeof(tv));
  tv.tv_usec = 20000;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setting UDP socket option");
    return -1;
  }
  
  sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr(host_name);
  sin.sin_port = htons(port);
  socklen_t sin_len = sizeof(sin);
  
  /* initialize buffer */
  char sendbuf[SEND_PACKET_SIZE];
  char recvbuf[RECV_PACKET_SIZE];
  unsigned short serial_no = 0;
  
  /* open file and get file size */
  struct stat s;
  char* filebuf;
  int file = open(file_name, O_RDONLY);
  fstat(file, &s);
  
  /* calculate map size */
  long long file_size = s.st_size;
  long long map_offset = 0;
  long long map_size = (file_size - map_offset) > MAX_MAP_SIZE ? MAX_MAP_SIZE : file_size - map_offset;
  bool retransmit = false;
  bool first_packet = true;
  
  while (map_offset < file_size) {
    /* map file into memory */
    filebuf = (char*)mmap(NULL, map_size, PROT_READ, MAP_SHARED, file, map_offset);
    if (filebuf == MAP_FAILED) {
      perror("map file failed!");
      return -1;
    }
    
    long long offset = 0;
    long long payload_size = (map_size - offset) > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : map_size - offset;
    
    while (offset < map_size) {
      if (!retransmit) {
        memset(sendbuf, 0, SEND_PACKET_SIZE);
        *(unsigned short*)(sendbuf + 4) = htons(serial_no);
        
        /* build first packet */
        if (first_packet) {
          *(unsigned short*)(sendbuf + 6) = htons(8 + strlen(file_name));
          unsigned int high = file_size >> 32;
          unsigned int low = file_size & 0x0ffffffff;
          *(unsigned int*)(sendbuf + 8) = htonl(high);
          *(unsigned int*)(sendbuf + 12) = htonl(low);
          memcpy(sendbuf + 16, file_name, strlen(file_name));
          unsigned int hash = DFTPHash(sendbuf + 4, 12 + strlen(file_name));
          *(unsigned int*)sendbuf = htonl(hash);
          
        /* build other packet */
        } else {
          unsigned short short_payload_size = payload_size & 0x0ffff;
          *(unsigned short*)(sendbuf + 6) = htons(short_payload_size);
          memcpy(sendbuf + 8, filebuf + offset, payload_size);
          unsigned int hash = DFTPHash(sendbuf + 4, 4 + payload_size);
          *(unsigned int*)sendbuf = htonl(hash);
        }
      }
      
      /* send packet */
      size_t buf_len = first_packet ? 16 + strlen(file_name) : 8 + payload_size;
      if (first_packet) {
        printf("[send data] file name: %s file size: %lld Bytes\n", file_name, file_size);
      } else {
        printf("[send data] %lld (%zu)\n", map_offset + offset, (size_t)payload_size);
      }
      long count = sendto(sockfd, sendbuf, buf_len, 0, (sockaddr*)&sin, sin_len);
      if (count < 0) {
        perror("error sending packet");
        return -1;
      }
      
      /* receive ACK from server */
      count = recvfrom(sockfd, recvbuf, RECV_PACKET_SIZE, 0, (sockaddr*)&sin, &sin_len);
      if (count < 0) {
        if (errno == EAGAIN) {
          printf("[timeout error]\n");
          retransmit = true;
          continue;
        } else {
          perror("error receiving packet");
          return -1;
        }
      }
      
      /* check serial number */
      if (serial_no != ntohs(*(unsigned short*)recvbuf)) {
        printf("[ACK packet error] local serial no: %d recv: %d\n", serial_no, ntohs(*(unsigned short*)recvbuf));
        retransmit = true;
        continue;
      } else {
        retransmit = false;
      }
      
      if (first_packet) {
        first_packet = false;
      } else {
        offset += payload_size;
        payload_size = (map_size - offset) > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : map_size - offset;
      }
      ++serial_no;
    }
    
    /* unmap file into memory */
    munmap(filebuf, map_size);
    map_offset += map_size;
    map_size = (file_size - map_offset) > MAX_MAP_SIZE ? MAX_MAP_SIZE : file_size - map_offset;
  }
  
  printf("[completed]\n");
  close(file);
  close(sockfd);
  return 0;
}