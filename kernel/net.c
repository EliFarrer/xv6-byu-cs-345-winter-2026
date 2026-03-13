#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock; // protects sockets

void
netinit(void)
{
  initlock(&netlock, "netlock");
  printf("netinit acquire netlock\n");
  acquire(&netlock);
  for (int i = 0; i < MAX_SOCKETS; i++) {
    initlock(&sockets[i].lock, "socketlock");
    sockets[i].port = 0;
  }
  release(&netlock);
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
// called before sys_recv()
//
uint64
sys_bind(void)
{
  int port;
  argint(0, &port);
  struct proc *p = myproc();
  return bind(port, p->pid);
}

uint64
bind(int port, int pid)
{
  int sock_idx;
  printf("bind: findsock\n");
  if ((sock_idx = find_sock(0, 0)) == -1) {
    return -1;
  }
  printf("bind: got_sock=%d\n", sock_idx);
  struct sock* socket = sockets + sock_idx;
  // initialize the queue
  socket->head = 0;
  socket->tail = 0;
  printf("bind: releasing socketlock\n");
  release(&socket->lock);
  // set the port and boundproc
  socket->port = port;
  socket->boundproc = pid;
  printf("bind: releasing netlock\n");
  release(&netlock);

  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  return 0;
}

// Searches the sockets for the next open, returns index if found, -1 otherwise
// acquires the netlock, optionally releases it, acquires the sockets queue lock
int
find_sock(int port, int release_net) {
  printf("\tfind_sock: acquire netlock\n");
  printf("\tfind_sock: port=%d\n", port);
  acquire(&netlock);
  for (int i = 0; i < MAX_SOCKETS; i++) {
    printf("\tfind_sock: test_port=%d\n", sockets[i].port);
    if (sockets[i].port == port) {
      printf("\tfind_sock: acquire socket lock\n");
      acquire(&sockets[i].lock);
      if (release_net) {
        printf("\tfind_sock: release netlock\n");
        release(&netlock);
      }
      return i;
    }
  }
  if (release_net) {
    printf("\tfind_sock: release netlock\n");
    release(&netlock);
  }
  // couldn't find any open sockets
  printf("\tfindsock: return\n");
  return -1;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// dport: destination port
// 
// returns the payload of a received UPD packet
// bind is called before this
//
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address (from the packet).
// sets *sport to the UDP source port (from the packet).
// copies up to maxlen bytes of UDP payload (from the packet) to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  struct proc *p = myproc();
  int dport;
  uint64 src;
  uint64 sport;
  uint64 bufaddr;
  int len;

  argint(0, &dport);
  argaddr(1, &src);
  argaddr(2, &sport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int out = recv(p, dport, src, sport, bufaddr, len);
  printf("sys_recv: got out=%d\n", out);
  return out;
}

uint64
recv(struct proc* p, int dport, uint64 src, uint64 sport, uint64 buf, int maxlen)
{
  int sock_idx;

  // can't find socket
  printf("starting recv findsock\n");
  if ((sock_idx = find_sock(dport, 1)) == -1) {
    return -1;
  }

  struct sock* socket = sockets + sock_idx;

  // sleep if the packet doesn't exist
  while (socket->head == socket->tail) {
    printf("recv: sleep on %p\n", socket);
    sleep(socket, &socket->lock);
  }
  
  // get the packet
  struct packet* packet = socket->queue[socket->head];

  // reset the head
  socket->head = (socket->head + 1) % MAX_QUEUE;
  release(&socket->lock);
  
  // return the length
  int copy_len = (packet->datalen > maxlen) ? maxlen : packet->datalen;
  copyout(p->pagetable, buf, packet->data, copy_len);                             // copy the payload
  copyout(p->pagetable, src, (char *) &packet->src_ip, sizeof(packet->src_ip));   // copy the source ip
  copyout(p->pagetable, sport, (char *) &packet->sport, sizeof(packet->sport));   // copy the source port

  kfree(packet->buf);
  kfree(packet);

  return copy_len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
// dst: host ip address
// dport: host port
// sport: source port
// buf: the payload
// len: length of payload
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("sys_send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}
 
// called by net_rk() for every received packet 
// decides if 1) packet is UDP, 2) port has been passed to bind
// saves the packet (max 16 per port), drops if full
// buffers it receives are:
// | ethernet header (14 bytes) | ip header (20 bytes) | UDP header (8 bytes) | 
void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  if (len < (sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp))) {
    printf("ip_rx: kfree\n");
    kfree(buf);
  } else {
    udp_rx(buf, len);
  }
}

void
udp_rx(char *buf, int len)
{
  struct ip *ip = (struct ip *) (buf + sizeof(struct eth));
  int header_len = (ip->ip_vhl & 0xf) << 2;
  struct udp *udp = (struct udp *) ((char *)ip + header_len);
  struct packet* packet;

  int socket_idx;

  printf("starting udp_rx findsock with port %d\n", ntohs(udp->dport));
  if ((socket_idx = find_sock(ntohs(udp->dport), 1)) == -1) {
    printf("udp_rx: socket is -1\n");
    kfree(buf);
    return;
  }

  struct sock* socket = sockets + socket_idx;
  
  // queue is full
  if ((socket->tail + 1) % MAX_QUEUE == socket->head) {
    printf("udp_rx: queue full\n");
    kfree(buf);
    release(&socket->lock);
    return;
  }
  
  if (socket == 0) {
    printf("udp_rx: socket is 0\n");
    kfree(buf);
    return;
  }

  // set the packet items
  if ((packet = kalloc()) == 0) {
    printf("udp_rx: kalloc failed\n");
    kfree(buf);
    release(&socket->lock);
    return;
  }
  
  printf("udp_rx: setting packet items\n");
  packet->src_ip = ntohl(ip->ip_src);
  packet->sport = ntohs(udp->sport);
  packet->buf = buf;
  packet->data = (char *) (udp + 1);
  packet->datalen = ntohs(udp->ulen) - sizeof(struct udp);

  // set the new tail
  socket->queue[socket->tail] = packet;
  socket->tail = (socket->tail + 1) % MAX_QUEUE;
  printf("udp_rx: wakeup on %p\n", socket);
  wakeup((void *) socket);
  printf("udp_rx: release\n");
  release(&socket->lock);
  printf("udp_rx: done\n");
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    printf("arp_rx: kfree");
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  printf("arp_rx: kfree inbuf\n");
  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    printf("net_rx: kfree\n");
    kfree(buf);
  }
}
