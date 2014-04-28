
#ifndef MCA_COLL_TUNED_SDN_UTIL_EXPORT_H
#define MCA_COLL_TUNED_SDN_UTIL_EXPORT_H

#include "ompi_config.h"

#include "mpi.h"
#include "coll_tuned.h"
#include "coll_tuned_topo.h"
#include "opal/mca/mca.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/request/request.h"
#include "ompi/mca/pml/pml.h"

#define DEFAULT_IF "tap0"
#define CONTROLLER_IP "163.221.29.195"
#define COMM_DST_PORT 10000
#define COMM_SRC_PORT 20000 

// Some constants that use in sdn_send_arp.
#define ETH_HDRLEN 14      // Ethernet header length
#define IP4_HDRLEN 20      // IPv4 header length
#define ARP_HDRLEN 28      // ARP header length
#define ARPOP_REQUEST 1    // Taken from <linux/if_arp.h>

BEGIN_C_DECLS

/* prototypes */
void sdn_open_send_rawsocket(int dst_rank);
void sdn_open_recv_rawsocket(int src_rank);
void sdn_send_data_rawsocket(int dst_rank, void *senddata, int datasize);
void sdn_recv_data_rawsocket(int src_rank, void *recvbuff, int datasize);

int sdn_send(void *buf, int count, MPI_Datatype datatype, int dest, int tag);
int sdn_recv(void *buf, int count, MPI_Datatype datatype, int source, int tag);

void sdn_init();
void sdn_finalize();

int sdn_send_arp();
void sdn_get_ip_address();

/* inline functions */

static inline unsigned short 
csum(unsigned short *buf, int nwords) {
  unsigned long sum;
  for(sum=0; nwords>0; nwords--)
    sum += *buf++;
    sum = (sum >> 16) + (sum &0xffff);
    sum += (sum >> 16);
  return (unsigned short)(~sum);
}

// Allocate memory for an array of chars. (use in sdn_send_arp)
static inline char *
allocate_strmem (int len) {
  void *tmp;

  if (len <= 0) {
    fprintf (stderr, "ERROR: Cannot allocate memory because len = %i in allocate_strmem().\n", len);
    exit (EXIT_FAILURE);
  }

  tmp = (char *) malloc (len * sizeof (char));
  if (tmp != NULL) {
    memset (tmp, 0, len * sizeof (char));
    return (tmp);
  } else {
    fprintf (stderr, "ERROR: Cannot allocate memory for array allocate_strmem().\n");
    exit (EXIT_FAILURE);
  }
}

// Allocate memory for an array of unsigned chars. (use in sdn_send_arp)
static inline uint8_t *
allocate_ustrmem (int len) {
  void *tmp;

  if (len <= 0) {
    fprintf (stderr, "ERROR: Cannot allocate memory because len = %i in allocate_ustrmem().\n", len);
    exit (EXIT_FAILURE);
  }

  tmp = (uint8_t *) malloc (len * sizeof (uint8_t));
  if (tmp != NULL) {
    memset (tmp, 0, len * sizeof (uint8_t));
    return (tmp);
  } else {
    fprintf (stderr, "ERROR: Cannot allocate memory for array allocate_ustrmem().\n");
    exit (EXIT_FAILURE);
  }
}


END_C_DECLS
#endif /* MCA_COLL_TUNED_SDN_UTIL_EXPORT_H */

