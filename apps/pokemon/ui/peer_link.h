#ifndef CABADGE_PEER_LINK_H
#define CABADGE_PEER_LINK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum { PEER_SINGLE,PEER_HOST,PEER_JOIN };
enum { PEER_OFF,PEER_WAITING,PEER_SCANNING,PEER_CONNECTING,PEER_READY,PEER_ERROR };
#define PEER_PACKET_BYTES 20
typedef struct {unsigned mode,status,epoch;int error;char room[9];} peer_link_info_t;
/* UI owner only; the existing NimBLE task validates and queues wire messages. */
bool peer_link_mode(unsigned mode);
bool peer_link_connect(unsigned index);
void peer_link_snapshot(peer_link_info_t *out);
bool peer_link_send(const uint8_t data[PEER_PACKET_BYTES]);
bool peer_link_receive(uint8_t data[PEER_PACKET_BYTES]);
#endif
