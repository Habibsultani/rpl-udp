#include "contiki.h"
#include "sys/etimer.h"
#include "sys/node-id.h"
#include "sys/log.h"

#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"

#include "dev/xmem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE "OTA-CLIENT"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define SENDER_NODE_ID 2
#define RELAY_NODE_ID 3

#define FIRMWARE_FILE "gonderilecek-guncel-firmware.z1"
#define FIRMWARE_SIZE 129760UL
/* Use raw Z1 external flash, not Coffee filenames. This avoids Coffee's
 * 15-character file-name limit and lets Cooja preload the flash backing file.
 */
#define FIRMWARE_XMEM_OFFSET (512UL * 1024UL)
#define CHUNK_SIZE 8
#define TOTAL_BLOCKS ((FIRMWARE_SIZE + CHUNK_SIZE - 1) / CHUNK_SIZE)

#define ACK_TIMEOUT (10 * CLOCK_SECOND)
#define RETRY_LIMIT 20

#define MSG_START     1
#define MSG_START_ACK 2
#define MSG_DATA      3
#define MSG_ACK       4
#define MSG_END       5
#define MSG_END_ACK   6

#define HEADER_SIZE 12
#define PACKET_SIZE (HEADER_SIZE + CHUNK_SIZE)

static struct simple_udp_connection udp_conn;
static uint8_t ack_received;
static uint8_t ack_type;
static uint32_t ack_block;

PROCESS(udp_client_process, "OTA UDP client");
AUTOSTART_PROCESSES(&udp_client_process);

/*---------------------------------------------------------------------------*/
static void
put_u16(uint8_t *buf, uint16_t value)
{
  buf[0] = (uint8_t)(value >> 8);
  buf[1] = (uint8_t)value;
}
/*---------------------------------------------------------------------------*/
static uint16_t
get_u16(const uint8_t *buf)
{
  return ((uint16_t)buf[0] << 8) | buf[1];
}
/*---------------------------------------------------------------------------*/
static void
put_u32(uint8_t *buf, uint32_t value)
{
  buf[0] = (uint8_t)(value >> 24);
  buf[1] = (uint8_t)(value >> 16);
  buf[2] = (uint8_t)(value >> 8);
  buf[3] = (uint8_t)value;
}
/*---------------------------------------------------------------------------*/
static uint32_t
get_u32(const uint8_t *buf)
{
  return ((uint32_t)buf[0] << 24) |
         ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) |
         buf[3];
}
/*---------------------------------------------------------------------------*/
static uint16_t
chunk_checksum(const uint8_t *data, uint8_t len)
{
  uint16_t sum = 0;
  uint8_t i;

  for(i = 0; i < len; i++) {
    sum = (uint16_t)(sum + data[i]);
  }

  return sum;
}
/*---------------------------------------------------------------------------*/
static uint32_t
firmware_checksum_update(uint32_t checksum, const uint8_t *data, uint8_t len)
{
  uint8_t i;

  /* FNV-1a: small, deterministic, and easy to re-run on both motes. */
  for(i = 0; i < len; i++) {
    checksum ^= data[i];
    checksum *= 16777619UL;
  }

  return checksum;
}
/*---------------------------------------------------------------------------*/
static uint16_t
make_packet(uint8_t *packet,
            uint8_t type,
            uint32_t block,
            uint32_t total_blocks,
            const uint8_t *payload,
            uint8_t len)
{
  packet[0] = type;
  put_u32(&packet[1], block);
  put_u32(&packet[5], total_blocks);
  packet[9] = len;

  if(len > 0 && payload != NULL) {
    memcpy(&packet[HEADER_SIZE], payload, len);
  }

  put_u16(&packet[10], chunk_checksum(&packet[HEADER_SIZE], len));
  return (uint16_t)(HEADER_SIZE + len);
}
/*---------------------------------------------------------------------------*/
static int
read_block(uint32_t block, uint8_t *buf, uint8_t *len)
{
  uint32_t offset = block * CHUNK_SIZE;
  uint8_t wanted = CHUNK_SIZE;
  int got;

  if(offset + CHUNK_SIZE > FIRMWARE_SIZE) {
    wanted = (uint8_t)(FIRMWARE_SIZE - offset);
  }

  got = xmem_pread(buf, wanted, FIRMWARE_XMEM_OFFSET + offset);
  if(got != wanted) {
    return -1;
  }

  /*
   * The Z1 xmem driver inverts bytes when reading from the external flash.
   * The Cooja preload rule writes the host firmware bytes directly into the
   * MSPSim flash backing file, so invert once here to recover the image byte.
   */
  for(got = 0; got < wanted; got++) {
    buf[got] ^= 0xff;
  }

  *len = (uint8_t)got;
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
calculate_full_checksum(uint32_t *checksum)
{
  static uint8_t buf[CHUNK_SIZE];
  uint32_t block;
  uint8_t len;

  *checksum = 2166136261UL;
  for(block = 0; block < TOTAL_BLOCKS; block++) {
    if(read_block(block, buf, &len) != 0) {
      LOG_ERR("xmem firmware is not fully available at block %lu.\n",
              (unsigned long)block);
      LOG_ERR("Preload %s into Node 2 external flash at offset %lu first.\n",
              FIRMWARE_FILE,
              (unsigned long)FIRMWARE_XMEM_OFFSET);
      return -1;
    }
    *checksum = firmware_checksum_update(*checksum, buf, len);
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
                const uip_ipaddr_t *sender_addr,
                uint16_t sender_port,
                const uip_ipaddr_t *receiver_addr,
                uint16_t receiver_port,
                const uint8_t *data,
                uint16_t datalen)
{
  uint16_t received_checksum;
  uint16_t expected_checksum;

  if(datalen < HEADER_SIZE) {
    return;
  }

  received_checksum = get_u16(&data[10]);
  expected_checksum = chunk_checksum(&data[HEADER_SIZE], data[9]);
  if(received_checksum != expected_checksum) {
    LOG_WARN("Ignoring ACK with bad checksum\n");
    return;
  }

  ack_type = data[0];
  if(ack_type == MSG_START_ACK || ack_type == MSG_ACK || ack_type == MSG_END_ACK) {
    ack_block = get_u32(&data[1]);
    ack_received = 1;
    LOG_INFO("ACK received for block %lu\n", (unsigned long)ack_block);
    process_poll(&udp_client_process);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer wait_timer;
  static struct etimer ack_timer;
  static uint8_t packet[PACKET_SIZE];
  static uint8_t block_buf[CHUNK_SIZE];
  static uint8_t meta[CHUNK_SIZE];
  static uip_ipaddr_t dest_ipaddr;
  static uint32_t firmware_checksum;
  static uint32_t block;
  static uint16_t packet_len;
  static uint8_t data_len;
  static uint8_t retries;

  PROCESS_BEGIN();

  simple_udp_register(&udp_conn,
                      UDP_CLIENT_PORT,
                      NULL,
                      UDP_SERVER_PORT,
                      udp_rx_callback);

  if(node_id == RELAY_NODE_ID) {
    LOG_INFO("Node 3 is relay only; it sends no application data.\n");
  }

  while(node_id != SENDER_NODE_ID) {
    etimer_set(&wait_timer, CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
  }

  LOG_INFO("Node 2 is OTA sender. Waiting for RPL root.\n");
  do {
    etimer_set(&wait_timer, CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
  } while(!NETSTACK_ROUTING.node_is_reachable() ||
          !NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr));

  if(calculate_full_checksum(&firmware_checksum) != 0) {
    PROCESS_EXIT();
  }

  LOG_INFO("Firmware size %lu bytes, total blocks %lu, checksum %lu\n",
           (unsigned long)FIRMWARE_SIZE,
           (unsigned long)TOTAL_BLOCKS,
           (unsigned long)firmware_checksum);

  put_u32(&meta[0], FIRMWARE_SIZE);
  put_u32(&meta[4], firmware_checksum);
  packet_len = make_packet(packet, MSG_START, 0, TOTAL_BLOCKS, meta, CHUNK_SIZE);

  ack_received = 0;
  retries = 0;
  while(!ack_received || ack_type != MSG_START_ACK) {
    if(ack_received && ack_type != MSG_START_ACK) {
      ack_received = 0;
    }
    LOG_INFO("Sending START for %lu blocks\n", (unsigned long)TOTAL_BLOCKS);
    simple_udp_sendto(&udp_conn, packet, packet_len, &dest_ipaddr);
    etimer_set(&ack_timer, ACK_TIMEOUT);
    PROCESS_WAIT_EVENT_UNTIL(ack_received || etimer_expired(&ack_timer));
    if(!ack_received && ++retries >= RETRY_LIMIT) {
      LOG_ERR("START ACK timeout limit reached\n");
      PROCESS_EXIT();
    }
  }

  for(block = 1; block <= TOTAL_BLOCKS; block++) {
    if(read_block(block - 1, block_buf, &data_len) != 0) {
      LOG_ERR("Could not read block %lu from xmem\n", (unsigned long)block);
      PROCESS_EXIT();
    }

    packet_len = make_packet(packet, MSG_DATA, block, TOTAL_BLOCKS,
                             block_buf, data_len);

    ack_received = 0;
    retries = 0;
    while(!ack_received || ack_type != MSG_ACK || ack_block != block) {
      if(ack_received && (ack_type != MSG_ACK || ack_block != block)) {
        ack_received = 0;
      }
      if(block <= 10 || (block % 100) == 0 || block == TOTAL_BLOCKS) {
        LOG_INFO("Sending firmware block %lu/%lu\n",
                 (unsigned long)block,
                 (unsigned long)TOTAL_BLOCKS);
      }
      simple_udp_sendto(&udp_conn, packet, packet_len, &dest_ipaddr);
      etimer_set(&ack_timer, ACK_TIMEOUT);
      PROCESS_WAIT_EVENT_UNTIL(ack_received || etimer_expired(&ack_timer));
      if(!ack_received && ++retries >= RETRY_LIMIT) {
        LOG_ERR("DATA ACK timeout at block %lu\n", (unsigned long)block);
        PROCESS_EXIT();
      }
    }

    if(block <= 10 || (block % 100) == 0 || block == TOTAL_BLOCKS) {
      LOG_INFO("Sent block %lu/%lu\n",
               (unsigned long)block,
               (unsigned long)TOTAL_BLOCKS);
    }
  }

  put_u32(&meta[0], FIRMWARE_SIZE);
  put_u32(&meta[4], firmware_checksum);
  packet_len = make_packet(packet, MSG_END, TOTAL_BLOCKS, TOTAL_BLOCKS,
                           meta, CHUNK_SIZE);

  ack_received = 0;
  retries = 0;
  while(!ack_received || ack_type != MSG_END_ACK) {
    if(ack_received && ack_type != MSG_END_ACK) {
      ack_received = 0;
    }
    LOG_INFO("Sending END\n");
    simple_udp_sendto(&udp_conn, packet, packet_len, &dest_ipaddr);
    etimer_set(&ack_timer, ACK_TIMEOUT);
    PROCESS_WAIT_EVENT_UNTIL(ack_received || etimer_expired(&ack_timer));
    if(!ack_received && ++retries >= RETRY_LIMIT) {
      LOG_ERR("END ACK timeout limit reached\n");
      PROCESS_EXIT();
    }
  }

  LOG_INFO("OTA firmware transfer finished.\n");

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
