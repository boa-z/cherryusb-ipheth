[English](architecture.md) | [简体中文](zh-CN/architecture.md)

# Architecture and lifetime

`src/ipheth.c` has no memory allocation, OS calls, USB transfers, unaligned integer reads, or dependencies beyond the C library. Callers supply complete USB RX transfers and synchronous frame consumers. NCM tables are completely validated before any callback is invoked, including termination, arithmetic, frame sizes and overlap. RX views cannot be retained after callback return.

The class parses the selected configuration, validates Apple VID and ff/fd/01, then chooses the lowest alternate with one bulk IN and one bulk OUT. No PID, USB configuration, interface number or endpoint address is fixed. With the current CherryUSB external class API, the class table matches the exact interface triplet and `connect` rejects non-Apple VID, because that API has only a combined VID/PID match table.

Each device owns its URBs, endpoint descriptors, DMA buffers, control setup, semaphores and TX mutex. EP0 setup does not reuse `hport->setup`; the stack's control-transfer lock serializes EP0 with other classes. No data transfer takes that shared EP0 mutex. USBMUX retains its own interface, private data and URBs.

A device worker submits asynchronous RX and receives completion through a semaphore; callbacks only publish results, never resubmit or touch the stack. The worker copies frames through the network adapter. Like Linux IPHETH, it waits for the first successful Bulk IN completion before polling carrier and publishing link-up. Successful empty transfers and four-byte 00 01 xx xx control notifications establish readiness but deliver no Ethernet frame. Carrier is then polled once a second using a wrapping monotonic clock. Sends before RX readiness/carrier-up return ENODEV and increment tx_deferred without submitting USB traffic. This observes data-path readiness; it implements no pairing/trust protocol. Reconnect resets the readiness state. USB faults retry with backoff; RX STALL clears the halt and resets the RX toggle. TX is synchronous with a 1 s timeout and a private mutex; TX STALL recovery is delegated to the worker.

On detach: mark stopping; wait for worker acknowledgment; cancel outstanding RX; report link down; drain network TX users; clear only this interface's private pointer; delete synchronization objects and recycle the slot. The worker's acknowledgment is its last access to device storage. A failed HCD cancellation quarantines the device slot and its objects instead of reusing possibly live DMA or callback memory. That deliberately costs a slot until reboot.

The RT-Thread adapter keeps its small `eth_device` objects and mutexes registered while unplugged. Queued tcpip packets can thus never refer to freed netif memory. Detached devices reject TX; reconnect reuses the device, updates its MAC in the lwIP thread and restarts IPv6 Link-Local DAD. No RX pbuf points into USB storage. A port must serialize all public send/stats users against its detach callback. Callbacks must not recursively disconnect the device or send from RX callbacks.

The current RT-Thread adapter expects its TCP/IP thread to have started before USB enumeration (the usual RT-Thread component initialization order). It does not force default-route changes. Socket users select the network interface or source address they intend to use.
