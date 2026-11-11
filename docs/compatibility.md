# Compatibility and limits

Build-checked with CherryUSB upstream commit
`a0054795c44037ace8a2b77b6e610adbff2257ea` and the Luban-Lite SDK's vendored
CherryUSB 1.5. Both use the external class registration API; neither requires a
core patch for this driver. The application may need its own configuration
selection hook to expose the desired interface.

The D213 RT-Thread/lwIP 2.1.2 adapter has a complete SDK cross-build. No physical
iPhone is currently validated by this repository. Host mocks are not evidence
of on-device transfer, successful trust or correct hardware cache behavior.

Provided board observation (external evidence, pending driver validation):

- D213 USB1 host, iPhone VID/PID 05ac:12a8.
- Configuration value 4, descriptor index 3.
- USBMUX interface 1; Apple Ethernet interface 2, ff/fd/01.
- Alt 0 without endpoints; Alt 1/2 IN 86 OUT 05, high-speed MPS 512.
- No standard CDC-NCM interface in the four reported configurations.

Those values are test observations, not hard-coded match requirements.

Protocol scope: untagged Ethernet with MTU 1500 and frames 14..1514 bytes.
Legacy RX strips a two-byte prefix. Successful vendor request 04 selects Apple
NCM16 RX; a STALL selects legacy RX. NCM16 uses NTH at 0 and NDP at 12 with up to
22 DPEs, fixed reserved header space of 108 bytes, NCM0 signature, no chained
NDPs and no CRC. The parser validates NTH/header/block/table lengths (including
64 KiB block-length wrap), termination and nonoverlapping bounded datagrams.
Devices with other layouts require captured descriptors/transfers and a tested
extension; do not silently guess a wire format. VLAN, jumbo frames, NTH32 and
NCM TX are outside this initial implementation.

Carrier handles both a one-byte 04 reply and replies with 04 in byte 1, as in
current Linux. Vendor request index follows the discovered interface, as in
FreeBSD, rather than assuming interface 2. Short/invalid MAC replies fail bind.
These protocol choices need real-device compatibility testing, particularly
CarPlay configuration behavior versus ordinary personal-hotspot operation.
