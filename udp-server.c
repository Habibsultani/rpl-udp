#include "contiki.h"
#include "sys/log.h"
#include "sys/node-id.h"

#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"

#include "dev/xmem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE "OTA-SERVER"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define ROOT_NODE_ID 1

#define OUTPUT_FILE "alinan-firmware.z1"
#define FIRMWARE_SIZE 129760UL
#define OUTPUT_XMEM_OFFSET (512UL * 1024UL)
#define OUTPUT_XMEM_ERASE_SIZE (128UL * 1024UL)
#define CHUNK_SIZE 8
#define TOTAL_BLOCKS ((FIRMWARE_SIZE + CHUNK_SIZE - 1) / CHUNK_SIZE)

#define MSG_START     1
#define MSG_START_ACK 2
#define MSG_DATA      3
#define MSG_ACK       4
#define MSG_END       5
#define MSG_END_ACK   6

#define HEADER_SIZE 12
#define PACKET_SIZE (HEADER_SIZE + CHUNK_SIZE)

static struct simple_udp_connection udp_conn;
static uint32_t expected_block;
static uint32_t last_acked_block;
static uint32_t received_bytes;
static uint32_t running_checksum;
static uint32_t expected_full_checksum;
static uint8_t transfer_started;
static uint8_t transfer_finished;

PROCESS(udp_server_process, "OTA UDP server");
AUTOSTART_PROCESSES(&udp_server_process);

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

  for(i = 0; i < len; i++) {
    checksum ^= data[i];
    checksum *= 16777619UL;
  }

  return checksum;
}
/*---------------------------------------------------------------------------*/
static uint16_t
make_ack(uint8_t *packet, uint8_t type, uint32_t block)
{
  packet[0] = type;
  put_u32(&packet[1], block);
  put_u32(&packet[5], TOTAL_BLOCKS);
  packet[9] = 0;
  put_u16(&packet[10], 0);
  return HEADER_SIZE;
}
/*---------------------------------------------------------------------------*/
static void
send_ack(const uip_ipaddr_t *receiver, uint8_t type, uint32_t block)
{
  static uint8_t ack[HEADER_SIZE];
  uint16_t len;

  len = make_ack(ack, type, block);
  simple_udp_sendto(&udp_conn, ack, len, receiver);
}
/*---------------------------------------------------------------------------*/
static void
reset_transfer(void)
{
  if(xmem_erase(OUTPUT_XMEM_ERASE_SIZE, OUTPUT_XMEM_OFFSET) < 0) {
    LOG_ERR("Could not erase xmem output area for %s\n", OUTPUT_FILE);
    transfer_started = 0;
    return;
  }

  expected_block = 1;
  last_acked_block = 0;
  received_bytes = 0;
  running_checksum = 2166136261UL;
  transfer_started = 1;
  transfer_finished = 0;
}
/*---------------------------------------------------------------------------*/
static void
handle_start(const uip_ipaddr_t *sender_addr, const uint8_t *payload, uint8_t len)
{
  uint32_t announced_size;

  if(len != CHUNK_SIZE) {
    LOG_WARN("START has invalid metadata length\n");
    return;
  }

  announced_size = get_u32(&payload[0]);
  expected_full_checksum = get_u32(&payload[4]);

  if(announced_size != FIRMWARE_SIZE) {
    LOG_WARN("START size mismatch: got %lu expected %lu\n",
             (unsigned long)announced_size,
             (unsigned long)FIRMWARE_SIZE);
    return;
  }

  if(!transfer_started || expected_block == 1) {
    reset_transfer();
  }

  if(transfer_started) {
    LOG_INFO("Started OTA receive: %lu blocks, checksum %lu\n",
             (unsigned long)TOTAL_BLOCKS,
             (unsigned long)expected_full_checksum);
    send_ack(sender_addr, MSG_START_ACK, 0);
  }
}
/*---------------------------------------------------------------------------*/
static void
handle_data(const uip_ipaddr_t *sender_addr,
            uint32_t block,
            const uint8_t *payload,
            uint8_t len,
            uint16_t received_checksum)
{
  int written;
  uint32_t offset;
  uint16_t calculated_checksum;
  uint8_t flash_buf[CHUNK_SIZE];
  uint8_t i;

  if(!transfer_started || transfer_finished) {
    return;
  }

  if(block == last_acked_block) {
    /* Duplicate DATA caused by a lost ACK. ACK it again. */
    LOG_INFO("Duplicate DATA block %lu, resending ACK\n",
             (unsigned long)block);
    send_ack(sender_addr, MSG_ACK, block);
    return;
  }

  if(block != expected_block) {
    LOG_WARN("Unexpected block %lu, expected %lu\n",
             (unsigned long)block,
             (unsigned long)expected_block);
    if(last_acked_block > 0) {
      LOG_INFO("Sending ACK for last good block %lu\n",
               (unsigned long)last_acked_block);
      send_ack(sender_addr, MSG_ACK, last_acked_block);
    }
    return;
  }

  calculated_checksum = chunk_checksum(payload, len);
  if(block <= 10 || (block % 100) == 0 || block == TOTAL_BLOCKS) {
    LOG_INFO("Received firmware block %lu/%lu, len=%u, checksum=%u, calculated=%u\n",
             (unsigned long)block,
             (unsigned long)TOTAL_BLOCKS,
             len,
             received_checksum,
             calculated_checksum);
  }

  offset = OUTPUT_XMEM_OFFSET + ((block - 1) * CHUNK_SIZE);
  /*
   * Z1 xmem stores the bitwise inverse of bytes passed to xmem_pwrite().
   * Store inverted bytes here so the host-visible .flash file contains the
   * raw firmware and can be extracted with dd/cmp after the simulation.
   */
  for(i = 0; i < len; i++) {
    flash_buf[i] = payload[i] ^ 0xff;
  }
  written = xmem_pwrite(flash_buf, len, offset);
  if(written != len) {
    LOG_ERR("xmem write failed/truncated at block %lu (%d/%u bytes)\n",
            (unsigned long)block,
            written,
            len);
    transfer_started = 0;
    return;
  }

  running_checksum = firmware_checksum_update(running_checksum, payload, len);
  received_bytes += len;
  last_acked_block = block;
  expected_block++;

  if(block <= 10 || (block % 100) == 0 || block == TOTAL_BLOCKS) {
    LOG_INFO("Stored block %lu to external-flash-slot-b. Flash size=%lu/%lu bytes\n",
             (unsigned long)block,
             (unsigned long)received_bytes,
             (unsigned long)FIRMWARE_SIZE);
    LOG_INFO("Sending ACK for block %lu\n", (unsigned long)block);
  }
  send_ack(sender_addr, MSG_ACK, block);
}
/*---------------------------------------------------------------------------*/
static void
handle_end(const uip_ipaddr_t *sender_addr, const uint8_t *payload, uint8_t len)
{
  static uint8_t verify_buf[CHUNK_SIZE];
  uint32_t announced_size;
  uint32_t announced_checksum;
  uint32_t stored_checksum;
  uint32_t block;
  uint8_t wanted;
  uint8_t i;
  uint32_t offset;
  int got;

  if(!transfer_started || len != CHUNK_SIZE) {
    return;
  }

  announced_size = get_u32(&payload[0]);
  announced_checksum = get_u32(&payload[4]);

  stored_checksum = 2166136261UL;
  for(block = 0; block < TOTAL_BLOCKS; block++) {
    offset = block * CHUNK_SIZE;
    wanted = CHUNK_SIZE;
    if(offset + CHUNK_SIZE > FIRMWARE_SIZE) {
      wanted = (uint8_t)(FIRMWARE_SIZE - offset);
    }

    got = xmem_pread(verify_buf, wanted, OUTPUT_XMEM_OFFSET + offset);
    if(got != wanted) {
      LOG_ERR("xmem verify read failed at block %lu\n", (unsigned long)block);
      transfer_started = 0;
      return;
    }
    /*
     * handle_data() stores host-raw bytes in the backing file. xmem_pread()
     * inverts flash bytes on read, so invert once more before checksumming.
     */
    for(i = 0; i < wanted; i++) {
      verify_buf[i] ^= 0xff;
    }
    stored_checksum = firmware_checksum_update(stored_checksum, verify_buf, wanted);
  }

  if(last_acked_block == TOTAL_BLOCKS &&
     received_bytes == FIRMWARE_SIZE &&
     announced_size == FIRMWARE_SIZE &&
     announced_checksum == expected_full_checksum &&
     running_checksum == expected_full_checksum &&
     stored_checksum == expected_full_checksum) {
    transfer_finished = 1;
    send_ack(sender_addr, MSG_END_ACK, TOTAL_BLOCKS);
    LOG_INFO("Received %lu/%lu blocks\n",
             (unsigned long)expected_block - 1,
             (unsigned long)TOTAL_BLOCKS);
    LOG_INFO("Full received firmware checksum: %lu\n",
             (unsigned long)running_checksum);
    LOG_INFO("Expected full firmware checksum: %lu\n",
             (unsigned long)expected_full_checksum);
    LOG_INFO("Flash stored firmware size: %lu/%lu bytes\n",
             (unsigned long)received_bytes,
             (unsigned long)FIRMWARE_SIZE);
    LOG_INFO("Yuklenmeye hazir yeni firmware alimi tamamlandi.\n");
  } else {
    LOG_ERR("END checksum/size mismatch. blocks=%lu bytes=%lu local=%lu stored=%lu sender=%lu\n",
            (unsigned long)expected_block,
            (unsigned long)received_bytes,
            (unsigned long)running_checksum,
            (unsigned long)stored_checksum,
            (unsigned long)expected_full_checksum);
    transfer_started = 0;
  }
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
  uint8_t type;
  uint32_t block;
  uint32_t total_blocks;
  uint8_t len;
  uint16_t received_checksum;
  uint16_t expected_checksum;
  const uint8_t *payload;

  if(node_id != ROOT_NODE_ID || datalen < HEADER_SIZE) {
    return;
  }

  type = data[0];
  block = get_u32(&data[1]);
  total_blocks = get_u32(&data[5]);
  len = data[9];
  received_checksum = get_u16(&data[10]);

  if(datalen != HEADER_SIZE + len || len > CHUNK_SIZE) {
    LOG_WARN("Ignoring malformed OTA packet\n");
    return;
  }

  payload = &data[HEADER_SIZE];
  expected_checksum = chunk_checksum(payload, len);
  if(received_checksum != expected_checksum) {
    LOG_WARN("Ignoring packet with bad checksum at block %lu\n",
             (unsigned long)block);
    return;
  }

  if(total_blocks != TOTAL_BLOCKS) {
    LOG_WARN("Ignoring packet with wrong total block count %lu\n",
             (unsigned long)total_blocks);
    return;
  }

  if(type == MSG_START) {
    handle_start(sender_addr, payload, len);
  } else if(type == MSG_DATA) {
    handle_data(sender_addr, block, payload, len, received_checksum);
  } else if(type == MSG_END) {
    handle_end(sender_addr, payload, len);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  if(node_id == ROOT_NODE_ID) {
    NETSTACK_ROUTING.root_start();
    LOG_INFO("Node 1 is RPL root and OTA receiver.\n");
  }

  simple_udp_register(&udp_conn,
                      UDP_SERVER_PORT,
                      NULL,
                      UDP_CLIENT_PORT,
                      udp_rx_callback);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
