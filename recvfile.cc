#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
using namespace std;

const int SEND_PACKET_SIZE = 2;
const int MAX_PAYLOAD_SIZE = 8192;
const int RECV_PACKET_SIZE = MAX_PAYLOAD_SIZE + 8;

unsigned int DFTPHash(const char *str, unsigned long len) {
  unsigned int seed = 131;
  unsigned int hash = 0;
  
  for (unsigned long i = 0; i < len; ++i) {
    hash = hash * seed + (*str++);
  }
  
  return hash;
}

int main(int argc, char** argv){
  /* get port number */
  int port=atoi(argv[2]);
  
  /* initialize server socket */
  int sockfd;
  if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("opening UDP socket!");
    return -1;
  }
  
  sockaddr_in servaddr, clienaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port        = htons(port);
  socklen_t addrlen = sizeof(clienaddr);
  
  /* bind server socket */
  if(::bind(sockfd, (sockaddr*) &servaddr, sizeof(servaddr)) < 0){
    perror("binding socket!");
    return -1;
  }
  
  /* initializ buffer */
  char sendbuf[SEND_PACKET_SIZE];
  char recvbuf[RECV_PACKET_SIZE];
  char file_name[1024];
  memset(file_name, 0, 1024);
  unsigned short serial_no = 0;
  
  int file = 0;
  bool first_packet = true;
  long long file_size = 0;
  long long offset = 0;
  
  while (true) {
    /* receive packet from client in blocking mode */
    long count = recvfrom(sockfd, recvbuf, RECV_PACKET_SIZE, 0, (sockaddr*)&clienaddr, &addrlen);
    if (count < 0) {
      perror("error receiving packet");
      break;
    }
    
    /* parse packet content */
    unsigned int recv_hash = ntohl(*(unsigned int*)recvbuf);
    unsigned short recv_serial_no = ntohs(*(unsigned short*)(recvbuf + 4));
    unsigned short payload_size = ntohs(*(unsigned short*)(recvbuf + 6));
    
    /* validation, check serial no, payload size and hash */
    if (count == payload_size + 8) {
      if (recv_serial_no == serial_no) {
        if (recv_hash == DFTPHash(recvbuf + 4, count - 4)) {
          /* initialize by first packet */
          if (first_packet && serial_no == 0) {
            /* get file size */
            file_size = ntohl(*(unsigned int*)(recvbuf + 8));
            file_size = file_size << 32;
            file_size += ntohl(*(unsigned int*)(recvbuf + 12));
            
            /* get file name */
            memcpy(file_name, recvbuf + 16, payload_size - 8);
            printf("[recv data] file name: %s file size: %lld Bytes\n", file_name, file_size);
            sprintf(file_name, "%s%s", file_name, ".recv");
            
            /* create the file */
            file = open(file_name, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
            if (file < 0) {
              perror("creating file failed");
              return -1;
            }
            
            first_packet = false;
            
            /* receive packet content */
          } else {
            printf("[recv data] %lld (%ld) ACCEPTED(in-order)\n", offset, (size_t)payload_size);
            if (write(file, recvbuf + 8, payload_size) < 0) {
              perror("writing file failed");
              return -1;
            }
            
            offset += payload_size;
          }
          
          /* send ACK */
          *(unsigned short*)sendbuf = htons(recv_serial_no);
          count = sendto(sockfd, sendbuf, SEND_PACKET_SIZE, 0, (sockaddr*)&clienaddr, addrlen);
          if (count < 0) {
            perror("error sending packet");
          }
          
          /* check whether receving all data */
          if (offset == file_size) {
            /* if receiving all data, send extra ACK */            
            for (int i = 0; i < 10; ++i) {
              *(unsigned short*)sendbuf = htons(recv_serial_no);
              count = sendto(sockfd, sendbuf, SEND_PACKET_SIZE, 0, (sockaddr*)&clienaddr, addrlen);
              if (count < 0) {
                perror("error sending packet");
              }
            }
            
            break;
          }
          
          ++serial_no;
        } else {
          printf("[recv corrupt packet]\n");
        }
      } else if (recv_serial_no < serial_no) {
        printf("[recv data] %lld (%ld) IGNORED\n", offset, (size_t)payload_size);
        
        /* send ACK */
        if (recv_serial_no + 1 == serial_no) {
        *(unsigned short*)sendbuf = htons(recv_serial_no);
        count = sendto(sockfd, sendbuf, SEND_PACKET_SIZE, 0, (sockaddr*)&clienaddr, addrlen);
        if (count < 0) {
          perror("error sending packet");
        }}
      }
    }
  }
  
  printf("[completed]\n");
  close(file);
  close(sockfd);
  return 0;
}