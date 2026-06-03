# rpl-udp

A simple RPL network with UDP communication. This is a self-contained example:
it includes a DAG root (`udp-server.c`) and DAG nodes (`udp-clients.c`).
This example runs without a border router -- this is a stand-alone RPL network.

The DAG root also acts as UDP server. The DAG nodes are UDP client. The clients
send a UDP request periodically, that simply includes a counter as payload.
When receiving a request, The server sends a response with the same counter
back to the originator.

The `.csc` files show example networks in the Cooja simulator, for sky motes and
for cooja motes.

For this example a "renode" make target is available, to run a 3 node
emulation in the Renode framework. For further instructions on installing and
using Renode please refer to [the documentation][1].

[1]: https://docs.contiki-ng.org/en/develop/doc/tutorials/Running-Contiki-NG-in-Renode.html

The rpl-udp.robot is a Robot framework test for renode. To run that do:

    >make TARGET=cc2538dk
    >renode-test rpl-udp.robot

## BIL304 Z1 OTA note

`BIL304-OS-Project-1.csc` uses emulated Z1 motes and must be built with
`TARGET=z1`. The full 129760-byte firmware is too large to embed as a C array
and Coffee file names are too short for `gonderilecek-guncel-firmware.z1`, so
the OTA sender reads the image from raw Z1 external flash (`xmem`) instead.

The `udp-client.z1` Makefile target preloads `gonderilecek-guncel-firmware.z1`
into the MSPSim flash backing file `build/z1/udp-client.flash` at offset
`524288`. Node 2 reads the full image from that xmem offset, sends it using the
START/DATA/ACK/END stop-and-wait protocol, and Node 1 stores the reconstructed
image in its own xmem at the same offset before verifying the checksum.

After the transfer finishes, Node 1's received image is host-visible in
`build/z1/udp-server.flash`. Extract the slot-B range and compare it with the
original firmware:

    dd if=build/z1/udp-server.flash of=received-firmware.z1 bs=1 skip=524288 count=129760
    cmp received-firmware.z1 gonderilecek-guncel-firmware.z1
    sha256sum received-firmware.z1 gonderilecek-guncel-firmware.z1

`udp-server.flash` is created by Cooja/MSPSim when the Z1 server mote writes to
external flash during the simulation.
