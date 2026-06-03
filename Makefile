CONTIKI_PROJECT = udp-client udp-server
all: $(CONTIKI_PROJECT)

CONTIKI=../..
include $(CONTIKI)/Makefile.include

FW_IMAGE = gonderilecek-guncel-firmware.z1
FW_FLASH = build/z1/udp-client.flash
FW_OFFSET = 524288

udp-client.z1: preload-z1-firmware

preload-z1-firmware: $(FW_IMAGE)
	@mkdir -p build/z1
	@dd if=$(FW_IMAGE) of=$(FW_FLASH) bs=1 seek=$(FW_OFFSET) conv=notrunc status=none
	@echo "Preloaded $(FW_IMAGE) into $(FW_FLASH) at xmem offset $(FW_OFFSET)"
