#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>           // close()
#include <string.h>           // strcpy, memset(), and memcpy()

#include <netdb.h>            // struct addrinfo
#include <sys/types.h>        // needed for socket(), uint8_t, uint16_t
#include <sys/socket.h>       // needed for socket()
#include <netinet/in.h>       // IPPROTO_RAW, INET_ADDRSTRLEN
#include <netinet/ip.h>       // IP_MAXPACKET (which is 65535)
#include <arpa/inet.h>        // inet_pton() and inet_ntop()
#include <sys/ioctl.h>        // macro ioctl is defined
#include <bits/ioctls.h>      // defines values for argument "request" of ioctl.
#include <net/if.h>           // struct ifreq
#include <linux/if_ether.h>   // ETH_P_ARP = 0x0806
#include <linux/if_packet.h>  // struct sockaddr_ll (see man 7 packet)
#include <net/ethernet.h>

#include "ompi_config.h"
#include "coll_tuned.h"

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/pml/pml.h"
#include "coll_tuned_sdn_util.h"

/* use in raw socket function */
struct ifreq _if_idx, _if_mac, _if_ip;


void sdn_open_send_rawsocket(int dst_rank) {
  int sockfd;

  /* Open raw socket to send data */
  if ((sockfd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1) {
    printf("Error : create raw socket\n");
  }

  /* Get the interface index */
  memset(&_if_idx, 0, sizeof(struct ifreq));
  strncpy(_if_idx.ifr_name, DEFAULT_IF, IFNAMSIZ-1);
  if (ioctl(sockfd, SIOCGIFINDEX, &_if_idx) < 0) {
    printf("Error : SIOCGIFINDEX\n");
  }

  /* Get MAC address of interface */
  memset(&_if_mac, 0, sizeof(struct ifreq));
  strncpy(_if_mac.ifr_name, DEFAULT_IF, IFNAMSIZ-1);
  if (ioctl(sockfd, SIOCGIFHWADDR, &_if_mac) < 0) {
    printf("Error : SIOCGIFHWADDR\n");
  }

  /* Get IP address of interface */
  memset(&_if_ip, 0, sizeof(struct ifreq));
  strncpy(_if_ip.ifr_name, DEFAULT_IF, IFNAMSIZ-1);
  if (ioctl(sockfd, SIOCGIFADDR, &_if_ip) < 0) {
    printf("Error : SIOCGIFADDR\n");
  }

  _send_socks[dst_rank] = sockfd;
}

void sdn_open_recv_rawsocket(int src_rank) {
  int sockfd;

  if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP))) < 0) {
    printf("Error : create raw socket\n");
  }

  _recv_sock = sockfd;
}

void sdn_send_data_rawsocket(int dst_rank, void *senddata, int datasize) {
  char sendbuf[1480];
  int bcast = 0;
  int i;
  int size, rank;

  size = ompi_comm_size(MPI_COMM_WORLD);
  rank = ompi_comm_rank(MPI_COMM_WORLD);

  if (dst_rank > size) {
    bcast = 1;
    // set dst_rank to other host (use next host here)
    dst_rank = (rank + 1) % size;
  }

  int sockfd = _send_socks[dst_rank];

  // TODO:
  if (datasize > 1300) {
    printf("Error: send_data_rawsocket\n");
  }

  /* Construct the Ethernet header */
  int tx_len = 0;
  struct ether_header *eh = (struct ether_header*) sendbuf;

  if (bcast) {
    eh->ether_shost[0] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[0];
    eh->ether_shost[1] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[1];
    eh->ether_shost[2] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[2];
    eh->ether_shost[3] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[3];
    eh->ether_shost[4] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[4];
    eh->ether_shost[5] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[5];

    eh->ether_dhost[0] = 0x00;
    eh->ether_dhost[1] = 0x00;
    eh->ether_dhost[2] = 0x00;
    eh->ether_dhost[3] = 0xFF;
    eh->ether_dhost[4] = 0xFF;
    eh->ether_dhost[5] = 0xFF;
  } else {
    eh->ether_shost[0] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[0];
    eh->ether_shost[1] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[1];
    eh->ether_shost[2] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[2];
    eh->ether_shost[3] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[3];
    eh->ether_shost[4] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[4];
    eh->ether_shost[5] = ((uint8_t *)&_if_mac.ifr_hwaddr.sa_data)[5];

    eh->ether_dhost[0] = _ether_hosts[dst_rank][0];
    eh->ether_dhost[1] = _ether_hosts[dst_rank][1];
    eh->ether_dhost[2] = _ether_hosts[dst_rank][2];
    eh->ether_dhost[3] = _ether_hosts[dst_rank][3];
    eh->ether_dhost[4] = _ether_hosts[dst_rank][4];
    eh->ether_dhost[5] = _ether_hosts[dst_rank][5];
  }
  
  eh->ether_type = htons(ETH_P_IP);
  tx_len += sizeof(struct ether_header);

  /* Construct the IP header */
  struct iphdr *iph = (struct iphdr *) (sendbuf + sizeof(struct ether_header));

  iph->ihl = 5;
  iph->version = 4;
  iph->tos = 16; // Low delay
  iph->id = htons(54321);
  iph->ttl = 200; // hops
  iph->protocol = 6; // TCP
  iph->saddr = inet_addr(inet_ntoa(((struct sockaddr_in *)&_if_ip.ifr_addr)->sin_addr));
  iph->daddr = inet_addr(_ip_hosts[dst_rank]);
  tx_len += sizeof(struct iphdr);

  struct tcpheader *tcp = (struct tcpheader *) (sendbuf + sizeof(struct iphdr) + sizeof(struct ether_header));
  
  tcp->tcph_srcport = htons(COMM_SRC_PORT);
  tcp->tcph_destport = htons(COMM_DST_PORT);
  tcp->tcph_seqnum = htonl(1);        ////////////////////
  tcp->tcph_acknum = 0;               ////////////////////
  tcp->tcph_offset = 5;               ////////////////////
  tcp->tcph_syn = 1;                  ////////////////////
  tcp->tcph_ack = 0;                  ////////////////////
  tcp->tcph_win = htons(32767);       ////////////////////
  tcp->tcph_chksum = 0; // Done by kernel
  tcp->tcph_urgptr = 0;
  tx_len += sizeof(struct tcpheader);

  /* TODO: add mpi header, tag, seq, etc. */

  /* Packet data */
  memcpy(sendbuf + tx_len, senddata, datasize);
  tx_len += datasize;

  /* Length of IP payload and header */
  iph->tot_len = htons(tx_len - sizeof(struct ether_header));
  /* Calculate IP checksum on completed header */
  iph->check = csum((unsigned short *)(sendbuf+sizeof(struct ether_header)), sizeof(struct iphdr)/2);

  /* Send the raw Ethernet packet */
  /* Destination address */
  struct sockaddr_ll socket_address;

  /* Index of the network device */
  socket_address.sll_ifindex = _if_idx.ifr_ifindex;
  /* Address length*/
  socket_address.sll_halen = ETH_ALEN;
  /* Destination MAC */
  socket_address.sll_addr[0] = 0x00;
  socket_address.sll_addr[1] = 0x00;
  socket_address.sll_addr[2] = 0x00;
  socket_address.sll_addr[3] = 0xFF;
  socket_address.sll_addr[4] = 0xFF;
  socket_address.sll_addr[5] = 0xFF;

  /*printf("\nPacket of rank %d\n", rank);
  for (i = 0; i < tx_len; i++) {
    printf("%d ", *(char*)(sendbuf+i));
    if (i % 20 == 0 && i != 0) printf("\n");
  } printf("\n");*/
  
  /* Send packet */
  int n = 0, sent = 0;
  if ((n = sendto(sockfd, sendbuf, tx_len, 0, (struct sockaddr*)&socket_address, sizeof(struct sockaddr_ll))) < 0) {
    printf("Send failed and err is %d\n", n);
  }
}

int sdn_send(void *buf, int count, MPI_Datatype datatype, int dest, int tag) {

    int type_size = 0;
    int send_size = 0;

    MPI_Type_size(datatype, &type_size);
    send_size = type_size * count;    

    // TODO: add tag
    sdn_send_data_rawsocket(dest, buf, send_size);

    return 0;
}

int sdn_recv(void *buf, int count, MPI_Datatype datatype, int source, int tag) {

    int type_size = 0;
    int recv_size = 0;

    MPI_Type_size(datatype, &type_size);
    recv_size = type_size * count;

    // TODO: check tag
    sdn_recv_data_rawsocket(source, buf, recv_size);

    return 0;
}


void parse_mac_address(int rank, char *mac_str) {
  int i = 0;
  int addr;
  char num[4];

  for (i = 0; i < 6; i++) {
    strncpy(num, mac_str+(i*3), 2);
    sscanf(num, "%x", &addr);
    _ether_hosts[rank][i] = (u_char)addr;
  }
}

void sdn_init() {

  if (sdn_comp_enable != 1) return;

  int p, root;
  int size, rank;

  size = ompi_comm_size(MPI_COMM_WORLD);
  rank = ompi_comm_rank(MPI_COMM_WORLD);

  //if (rank == 0) printf("Enable SDN MPI\n");

  sdn_get_ip_address();
  sdn_send_arp();

  int sockfd,n;
  struct sockaddr_in servaddr,cliaddr;
  char sendline[2000];
  char buffer[2000];
  char *next_start;

  sockfd=socket(AF_INET,SOCK_STREAM,0);

  bzero(&servaddr,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(CONTROLLER_IP);
  servaddr.sin_port = htons(65432);

  connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

  /* Send data to controller */
  // public_ip, rank
  sendline[0] = '\0';
  sprintf(sendline, "%s %s %d %d", _ip, _mac_addr, rank, size);
  sendto(sockfd, sendline, strlen(sendline), 0,
    (struct sockaddr *)&servaddr, sizeof(servaddr));

  /* Receive reduction plan from controller */
  n = recvfrom(sockfd, buffer, 2000, 0, NULL, NULL);
  buffer[n] = '\0';
  next_start = buffer;

  int num;
  char str_buf[100];

  // skip number of processes
  next_start = split_first_string(next_start, str_buf); 

  for (p = 0; p < size; p++) {
    // root
    next_start = split_first_string(next_start, str_buf);
    int root = str_to_int(str_buf);

    // step count
    next_start = split_first_string(next_start, str_buf);
    num = str_to_int(str_buf);

    _plan_count[root] = num;

    int pc;
    for (pc = 0; pc < _plan_count[root]; pc++) {
      next_start = split_first_string(next_start, str_buf);
      _sr_plans[root][pc].src = str_to_int(str_buf);
      next_start = split_first_string(next_start, str_buf);
      _sr_plans[root][pc].dst = str_to_int(str_buf);
    }
  }

  /* Contruct shortest binomial tree for each root */
  for (p = 0; p < size; p++) {
    ompi_coll_tuned_topo_build_shortest_bmtree(MPI_COMM_WORLD, sdn_shortest_bmtree[p], p);
  }

  /* Receive MAC address of all nodes */
  n = recvfrom(sockfd, buffer, 2000, 0, NULL, NULL);
  buffer[n] = '\0';
  next_start = buffer;

  for (p = 0; p < size; p++) {
    next_start = split_first_string(next_start, str_buf);
    int recv_rank = str_to_int(str_buf);

    next_start = split_first_string(next_start, str_buf);
    parse_mac_address(recv_rank, str_buf);

    next_start = split_first_string(next_start, str_buf);
    strcpy(_ip_hosts[recv_rank], str_buf);
  }

  /* dump receieve data */
  /*if (rank == 0) {
    for (p = 0; p < size; p++)
      printf("[Rank %d] private ip is %s\n", p, _ip_hosts[p]);
  }*/

  /* Socket for communication between host */
  for (p = 0; p < size; p++) {
    if (p != rank) sdn_open_send_rawsocket(p);
  }
  sdn_open_recv_rawsocket(-1);
}

void sdn_finalize() {

  if (sdn_comp_enable != 1) return;

  int p;
  int size, rank;

  size = ompi_comm_size(MPI_COMM_WORLD);
  rank = ompi_comm_rank(MPI_COMM_WORLD);

  for (p = 0; p < size; p++) {
    if (p != rank) {
      //close(_send_socks[p]);
    }
  }

  //close(_recv_sock);
}


// Define a struct for ARP header
typedef struct _arp_hdr arp_hdr;
struct _arp_hdr {
  uint16_t htype;
  uint16_t ptype;
  uint8_t hlen;
  uint8_t plen;
  uint16_t opcode;
  uint8_t sender_mac[6];
  uint8_t sender_ip[4];
  uint8_t target_mac[6];
  uint8_t target_ip[4];
};

int sdn_send_arp() {
  int i, status, frame_length, sd, bytes;
  char *interface, *target, *src_ip;
  arp_hdr arphdr;
  uint8_t *src_mac, *dst_mac, *ether_frame;
  struct addrinfo hints, *res;
  struct sockaddr_in *ipv4;
  struct sockaddr_ll device;
  struct ifreq ifr;

  // Allocate memory for various arrays.
  src_mac = allocate_ustrmem (6);
  dst_mac = allocate_ustrmem (6);
  ether_frame = allocate_ustrmem (IP_MAXPACKET);
  interface = allocate_strmem (40);
  target = allocate_strmem (40);
  src_ip = allocate_strmem (INET_ADDRSTRLEN);

  // Interface to send packet through.
  strcpy (interface, "tap0");

  // Submit request for a socket descriptor to look up interface.
  if ((sd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
    perror ("socket() failed to get socket descriptor for using ioctl() ");
    exit (EXIT_FAILURE);
  }

  // Use ioctl() to look up interface name and get its MAC address.
  memset (&ifr, 0, sizeof (ifr));
  snprintf (ifr.ifr_name, sizeof (ifr.ifr_name), "%s", interface);
  if (ioctl (sd, SIOCGIFHWADDR, &ifr) < 0) {
    perror ("ioctl() failed to get source MAC address ");
    return (EXIT_FAILURE);
  }
  close (sd);

  // Copy source MAC address.
  memcpy (src_mac, ifr.ifr_hwaddr.sa_data, 6 * sizeof (uint8_t));
  sprintf(_mac_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
    (unsigned char)ifr.ifr_hwaddr.sa_data[0],
    (unsigned char)ifr.ifr_hwaddr.sa_data[1],
    (unsigned char)ifr.ifr_hwaddr.sa_data[2],
    (unsigned char)ifr.ifr_hwaddr.sa_data[3],
    (unsigned char)ifr.ifr_hwaddr.sa_data[4],
    (unsigned char)ifr.ifr_hwaddr.sa_data[5]);

  // Report source MAC address to stdout.
  /*printf ("MAC address for interface %s is ", interface);
  for (i=0; i<5; i++) {
    printf ("%02x:", src_mac[i]);
  }
  printf ("%02x\n", src_mac[5]);*/

  // Find interface index from interface name and store index in
  // struct sockaddr_ll device, which will be used as an argument of sendto().
  memset (&device, 0, sizeof (device));
  if ((device.sll_ifindex = if_nametoindex (interface)) == 0) {
    perror ("if_nametoindex() failed to obtain interface index ");
    exit (EXIT_FAILURE);
  }
  //printf ("Index for interface %s is %i\n", interface, device.sll_ifindex);

  // Set destination MAC address: broadcast address
  memset (dst_mac, 0xff, 6 * sizeof (uint8_t));

  // Source IPv4 address:  you need to fill this out
  strcpy (src_ip, _ip);

  // Destination URL or IPv4 address (must be a link-local node): you need to fill this out
  strcpy (target, _ip);

  // Fill out hints for getaddrinfo().
  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = hints.ai_flags | AI_CANONNAME;

  // Source IP address
  if ((status = inet_pton (AF_INET, src_ip, &arphdr.sender_ip)) != 1) {
    fprintf (stderr, "inet_pton() failed for source IP address.\nError message: %s", strerror (status));
    exit (EXIT_FAILURE);
  }

  // Resolve target using getaddrinfo().
  if ((status = getaddrinfo (target, NULL, &hints, &res)) != 0) {
    fprintf (stderr, "getaddrinfo() failed: %s\n", gai_strerror (status));
    exit (EXIT_FAILURE);
  }
  ipv4 = (struct sockaddr_in *) res->ai_addr;
  memcpy (&arphdr.target_ip, &ipv4->sin_addr, 4 * sizeof (uint8_t));
  freeaddrinfo (res);

  // Fill out sockaddr_ll.
  device.sll_family = AF_PACKET;
  memcpy (device.sll_addr, src_mac, 6 * sizeof (uint8_t));
  device.sll_halen = htons (6);

  // ARP header

  // Hardware type (16 bits): 1 for ethernet
  arphdr.htype = htons (1);

  // Protocol type (16 bits): 2048 for IP
  arphdr.ptype = htons (ETH_P_IP);

  // Hardware address length (8 bits): 6 bytes for MAC address
  arphdr.hlen = 6;

  // Protocol address length (8 bits): 4 bytes for IPv4 address
  arphdr.plen = 4;

  // OpCode: 1 for ARP request
  arphdr.opcode = htons (ARPOP_REQUEST);

  // Sender hardware address (48 bits): MAC address
  memcpy (&arphdr.sender_mac, src_mac, 6 * sizeof (uint8_t));

  // Sender protocol address (32 bits)
  // See getaddrinfo() resolution of src_ip.

  // Target hardware address (48 bits): zero, since we don't know it yet.
  memset (&arphdr.target_mac, 0, 6 * sizeof (uint8_t));

  // Target protocol address (32 bits)
  // See getaddrinfo() resolution of target.

  // Fill out ethernet frame header.

  // Ethernet frame length = ethernet header (MAC + MAC + ethernet type) + ethernet data (ARP header)
  frame_length = 6 + 6 + 2 + ARP_HDRLEN;

  // Destination and Source MAC addresses
  memcpy (ether_frame, dst_mac, 6 * sizeof (uint8_t));
  memcpy (ether_frame + 6, src_mac, 6 * sizeof (uint8_t));

  // Next is ethernet type code (ETH_P_ARP for ARP).
  // http://www.iana.org/assignments/ethernet-numbers
  ether_frame[12] = ETH_P_ARP / 256;
  ether_frame[13] = ETH_P_ARP % 256;

  // Next is ethernet frame data (ARP header).

  // ARP header
  memcpy (ether_frame + ETH_HDRLEN, &arphdr, ARP_HDRLEN * sizeof (uint8_t));

  // Submit request for a raw socket descriptor.
  if ((sd = socket (PF_PACKET, SOCK_RAW, htons (ETH_P_ALL))) < 0) {
    perror ("socket() failed ");
    exit (EXIT_FAILURE);
  }

  // Send ethernet frame to socket.
  if ((bytes = sendto (sd, ether_frame, frame_length, 0, (struct sockaddr *) &device, sizeof (device))) <= 0) {
    perror ("sendto() failed");
    exit (EXIT_FAILURE);
  }

  // Close socket descriptor.
  close (sd);

  // Free allocated memory.
  free (src_mac);
  free (dst_mac);
  free (ether_frame);
  free (interface);
  free (target);
  free (src_ip);

  return 0;
}

void sdn_get_ip_address() {

  const int domain = AF_INET;

  int s;
  struct ifconf ifconf;
  struct ifreq ifr[50];
  int ifs;
  int i;

  s = socket(domain, SOCK_STREAM, 0);
  if (s < 0) {
    perror("socket");
  }

  ifconf.ifc_buf = (char *) ifr;
  ifconf.ifc_len = sizeof ifr;

  if (ioctl(s, SIOCGIFCONF, &ifconf) == -1) {
    perror("ioctl");
  }

  ifs = ifconf.ifc_len / sizeof(ifr[0]);
  for (i = 0; i < ifs; i++) {
    if (strcmp(ifr[i].ifr_name, DEFAULT_IF))
      continue;
    struct sockaddr_in *s_in = (struct sockaddr_in *) &ifr[i].ifr_addr;

    if (!inet_ntop(domain, &s_in->sin_addr, _ip, sizeof(_ip))) {
      perror("inet_ntop");
    }
  }

  close(s);
}
