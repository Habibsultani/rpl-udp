# BIL304 – OTA Firmware Update over RPL

## Project Overview

This project was developed for the **BIL304 Operating Systems** course using **Contiki-NG** and the **Cooja Simulator**.

The objective is to implement a reliable **Over-The-Air (OTA) firmware update mechanism** over an RPL-based wireless sensor network.

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

## Firmware Information

```text
Firmware File : gonderilecek-guncel-firmware.z1
Firmware Size : 129760 Bytes (~127 KB)
Architecture  : TI MSP430
Format        : ELF32
```

---

## OTA Protocol

The firmware is divided into fixed-size blocks and transmitted using a custom UDP-based OTA protocol.

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

Reliability is achieved through:

* ACK packets
* Timeout detection
* Retransmission
* Checksum verification

---

## Results

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
```

The firmware was successfully transferred, reconstructed, stored in simulated flash memory, and verified.

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

## Technologies

* Contiki-NG
* Cooja Simulator
* RPL Lite
* UDP
* MSP430
* ELF Analysis Tools

---

## Project Team

* Habib Sultani
* Rasha Muhammed Ali
* Abdullah Fawzi Saad Al Rayyis
* Saad

---

## Course

**BIL304 – Operating Systems**
**Ondokuz Mayıs University**
