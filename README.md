# BIL304 – OTA Firmware Update over RPL

## Project Overview

This project was developed for the **BIL304 Operating Systems** course using **Contiki-NG** and the **Cooja Simulator**.

The objective of this project is to implement a reliable **Over-The-Air (OTA) firmware update mechanism** over an RPL-based wireless sensor network. The system demonstrates how a firmware image can be fragmented, transmitted, verified, and stored on a remote device through a multi-hop IoT network.

---
Project Demonstration Video

YouTube Video Link:

https://youtu.be/L5Fxu_aonlQ

The video includes:

Cooja simulation demonstration
Team member presentations
OTA workflow explanation
Checksum verification explanation
Final successful firmware transfer output
Research Repository

Repository Link:

https://github.com/SAAD-MOHAMMEDD/ota-toolchain-anaysis



---

## Why OTA Updates?

In large-scale IoT systems, manually updating devices is expensive and time-consuming.

OTA (Over-The-Air) updates allow firmware to be distributed remotely through a network without physical access to devices.

Advantages:

* Remote firmware deployment
* Faster bug fixes
* Security patch distribution
* Reduced maintenance cost
* Scalable device management

This project demonstrates a simplified OTA firmware update workflow using Contiki-NG.

---

## Network Topology

| Node   | Role                    |
| ------ | ----------------------- |
| Node 1 | RPL Root & OTA Receiver |
| Node 2 | OTA Sender              |
| Node 3 | Relay Node              |

```text
Node 2  --->  Node 3  --->  Node 1
(Sender)     (Relay)      (Receiver)
```

---

## System Architecture

```text
                RPL Network

     Node 2                Node 3                Node 1
  (OTA Sender)  ----->   (Relay)   ----->   (RPL Root)

        |                                      |
        |                                      |
        +---- Firmware Transfer -------------->|
                                               |
                                               v
                                       External Flash
                                           (xmem)
```

Node 2 reads the firmware image and transmits it through the RPL network.

Node 3 acts as an intermediate forwarding node.

Node 1 receives all firmware blocks, stores them into external flash memory, verifies integrity, and prepares the image for installation.

---

## Firmware Information

```text
Firmware File : gonderilecek-guncel-firmware.z1
Firmware Size : 129760 Bytes (~127 KB)
Architecture  : TI MSP430
Format        : ELF32
```

---

## Implemented Method

The original Contiki-NG `rpl-udp` example only transmitted simple text messages:

```text
Merhaba 1
Merhaba 2
Merhaba 3
```

In this project, the example was extended to support real OTA firmware transfer.

The firmware image is fragmented into fixed-size blocks and transmitted over UDP.

---

## OTA Protocol

### Packet Types

* START
* DATA
* ACK
* END

### Transfer Parameters

```text
Block Size   : 8 Bytes (64 Bits)
Total Blocks : 16220
```

---

## Packet Structure

### START Packet

| Field         | Description         |
| ------------- | ------------------- |
| Type          | START               |
| Firmware Size | Total firmware size |
| Block Count   | Number of blocks    |
| Checksum      | Expected checksum   |

### DATA Packet

| Field        | Description     |
| ------------ | --------------- |
| Type         | DATA            |
| Block Number | Sequence number |
| Length       | Payload length  |
| Checksum     | Block checksum  |
| Payload      | Firmware bytes  |

### ACK Packet

| Field        | Description        |
| ------------ | ------------------ |
| Type         | ACK                |
| Block Number | Acknowledged block |

### END Packet

| Field              | Description |
| ------------------ | ----------- |
| Type               | END         |
| Transfer completed |             |

---

## Transmission Flow

```text
START
  ↓
DATA Block 1
  ↓
ACK
  ↓
DATA Block 2
  ↓
ACK
  ↓
...
  ↓
DATA Block 16220
  ↓
ACK
  ↓
END
```

---

## Reliability Mechanisms

UDP does not guarantee reliable delivery. Therefore, the following mechanisms were implemented:

* Block numbering
* ACK packets
* Timeout detection
* Retransmission
* Checksum verification
* Complete firmware validation

These mechanisms ensure that missing or corrupted packets can be detected and retransmitted.

---

## Sample Code Snippets

### Sending a Firmware Block

```c
simple_udp_sendto(&udp_conn,
                  packet,
                  packet_len,
                  &dest_ipaddr);
```

### ACK Verification

```c
if(waiting_for_ack) {
  retransmit_block();
}
```

### Writing to Flash

```c
xmem_pwrite(buffer,
            length,
            offset);
```

### Integrity Verification

```c
if(received_checksum == expected_checksum) {
  firmware_valid = 1;
}
```

---

## Firmware Storage

The received firmware is stored inside simulated external flash memory (xmem).

```text
Storage Offset : 524288
```

Node 1 reconstructs the firmware image and stores it into external flash before performing integrity verification.

---

## Checksum Verification

To ensure firmware integrity, checksum verification is used.

Process:

1. Sender calculates firmware checksum.
2. Receiver reconstructs the image.
3. Receiver calculates checksum again.
4. Both values are compared.

If:

```text
Expected Checksum == Calculated Checksum
```

the firmware is accepted.

Otherwise, the update is rejected.

---

## Simulation Results

Successful transfer output:

```text
Received 16220/16220 blocks

Full received firmware checksum:
1752943717

Expected full firmware checksum:
1752943717

Flash stored firmware size:
129760/129760 bytes

Yuklenmeye hazir yeni firmware alimi tamamlandi.

OTA firmware transfer finished.
```

The firmware was successfully transferred, reconstructed, stored in flash memory, and verified.

---

## ELF Analysis

The transferred firmware was analyzed using:

```bash
file
readelf
objdump
nm
size
```

Results:

```text
Architecture : TI MSP430
Format       : ELF32

.text        : 71715 Bytes
.data        : 336 Bytes
.bss         : 5706 Bytes

Total Size   : 77757 Bytes
```

Important sections:

| Section  | Description             |
| -------- | ----------------------- |
| .text    | Executable code         |
| .data    | Initialized variables   |
| .bss     | Uninitialized variables |
| .rodata  | Read-only constants     |
| .vectors | Interrupt vectors       |

---

## Build

```bash
make clean TARGET=z1

make udp-client.z1 TARGET=z1

make udp-server.z1 TARGET=z1
```

---

## Run Simulation

```bash
cd ~/contiki-ng/tools/cooja

./gradlew run --args='--gui ../../examples/rpl-udp/BIL304-OS-Project-1.csc'
```

---

## How to Run the Project

### Prerequisites

Required software:

* Ubuntu / WSL
* Java JDK 17+
* Git
* Contiki-NG
* Cooja Simulator

### Running on Windows (WSL)

```powershell
wsl
```

```bash
cd ~/contiki-ng/examples/rpl-udp

make clean TARGET=z1

make udp-client.z1 TARGET=z1

make udp-server.z1 TARGET=z1
```

```bash
cd ~/contiki-ng/tools/cooja

./gradlew run --args='--gui ../../examples/rpl-udp/BIL304-OS-Project-1.csc'
```

### Running on Linux

```bash
git clone https://github.com/contiki-ng/contiki-ng.git

cd contiki-ng/examples/rpl-udp

make clean TARGET=z1

make udp-client.z1 TARGET=z1

make udp-server.z1 TARGET=z1
```

```bash
cd ~/contiki-ng/tools/cooja

./gradlew run --args='--gui ../../examples/rpl-udp/BIL304-OS-Project-1.csc'
```

---

## Expected Output

```text
Received 16220/16220 blocks

Full received firmware checksum:
1752943717

Expected full firmware checksum:
1752943717

Flash stored firmware size:
129760/129760 bytes

Yuklenmeye hazir yeni firmware alimi tamamlandi.

OTA firmware transfer finished.
```

---

## Technologies

* Contiki-NG
* Cooja Simulator
* RPL Lite
* UDP
* IPv6
* MSP430
* ELF Analysis Tools
* Linux / WSL

---


## Project Team

* Habib Sultani
* Rasha Muhammed Ali
* Abdullah Fawzi Saad Al Rayyis
* Saad Mohammed Abderezak

---

## Course

**BIL304 – Operating Systems**
**Ondokuz Mayıs University**
