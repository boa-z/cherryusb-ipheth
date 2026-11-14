[English](compatibility.md) | [简体中文](zh-CN/compatibility.md)

# Compatibility and limits

Validated in host builds against official CherryUSB 1.6.1 master commit `a0054795c44037ace8a2b77b6e610adbff2257ea`. Luban-Lite now vendors this exact upstream tree, with SDK integration outside it. No core patch is required by this external class driver. Configuration selection remains application policy.

The old SDK 1.5 control API included eight Setup bytes in its return value; the first hardware attempt exposed this mismatch and failed Ethernet init, which in turn caused the old enumerator to detach USBMUX. That stack is no longer a supported runtime target. The official data-only EP0 return contract is covered by a regression test compiling the actual upstream core. Recognized Apple Ethernet initialization failures now leave the class inactive and retain other composite interfaces, with a diagnostic identifying the step. Replug after fixing the prerequisite to retry initialization.

The D213 RT-Thread/lwIP 2.1.2 adapter has a complete SDK cross-build. Supplied v13 UART logs validate binding, bidirectional Ethernet traffic, DHCP and a valid local IPv6 Link-Local address after external host activation. Subsequent application logs show a real RTSP exchange over the advertised wired IPv6 endpoint. This is one observed device/configuration, not a compatibility claim for all iPhones. One RX error, sustained load and unplug/replug stress remain open. Host mocks alone do not validate on-device cache behavior.

Provided board observations (external UART evidence):

- D213 USB1 host, iPhone VID/PID 05ac:12a8.
- Configuration value 4, descriptor index 3.
- USBMUX interface 1; Apple Ethernet interface 2, ff/fd/01.
- Alt 0 without endpoints; Alt 1/2 IN 86 OUT 05, high-speed MPS 512.
- No standard CDC-NCM interface in the four reported configurations.

Those values are test observations, not hard-coded match requirements.

Protocol scope: untagged Ethernet with MTU 1500 and frames 14..1514 bytes. Legacy RX strips a two-byte prefix. Successful vendor request 04 selects Apple NCM16 RX; a STALL selects legacy RX. NCM16 uses NTH at 0 and NDP at 12 with up to 22 DPEs, fixed reserved header space of 108 bytes, NCM0 signature, no chained NDPs and no CRC. The parser validates NTH/header/block/table lengths (including 64 KiB block-length wrap), termination and nonoverlapping bounded datagrams. Devices with other layouts require captured descriptors/transfers and a tested extension; do not silently guess a wire format. VLAN, jumbo frames, NTH32 and NCM TX are outside this initial implementation.

Carrier handles both a one-byte 04 reply and replies with 04 in byte 1, as in current Linux. Vendor request index follows the discovered interface, as in FreeBSD, rather than assuming interface 2. Short/invalid MAC replies fail bind. These protocol choices need real-device compatibility testing, particularly CarPlay configuration behavior versus ordinary personal-hotspot operation.
