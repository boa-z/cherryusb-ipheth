/* SPDX-License-Identifier: Apache-2.0 */
#ifndef IPHETH_H
#define IPHETH_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define IPHETH_FRAME_MAX 1514u
#define IPHETH_RX_LEGACY_MAX 1516u
#define IPHETH_RX_NCM_MAX 65536u
#define IPHETH_CTRL_SIZE 64u
#define IPHETH_APPLE_VID 0x05acu
#define IPHETH_CMD_MAC 0x00u
#define IPHETH_CMD_NCM 0x04u
#define IPHETH_CMD_CARRIER 0x45u
/* Errors are independent of errno and of the USB/OS adapters. */
enum { IPHETH_EINVAL = -1, IPHETH_EIO = -2, IPHETH_ENODEV = -3 };
enum ipheth_rx_mode { IPHETH_LEGACY, IPHETH_NCM };
struct ipheth_interface {
    uint8_t number, alternate, in, out;
    uint16_t in_mps, out_mps;
};
struct ipheth_request {
    uint8_t type, request;
    uint16_t value, index, length;
};
typedef int (*ipheth_frame_fn)(void *, const uint8_t *, size_t);
/* Diagnostics do not change validation, callback order, or return codes.
 * A rejected block is validated before delivery. A callback error can leave
 * later validated datagrams undelivered; delivered counts successful callbacks. */
enum ipheth_rx_reason {
    IPHETH_RX_OK, IPHETH_RX_ARGUMENT, IPHETH_RX_SIZE, IPHETH_RX_SIGNATURE,
    IPHETH_RX_HEADER, IPHETH_RX_BLOCK, IPHETH_RX_TABLE, IPHETH_RX_RANGE,
    IPHETH_RX_FRAME, IPHETH_RX_OVERLAP, IPHETH_RX_TERMINATOR,
    IPHETH_RX_CALLBACK, IPHETH_RX_REASON_COUNT
};
struct ipheth_rx_diagnostic {
    enum ipheth_rx_reason reason;
    unsigned index, offset, length, validated, delivered;
};
/* Parses a single selected configuration, preferring the lowest usable alt.
 * Does not select a USB configuration or claim any other interface. */
int ipheth_find_interface(uint16_t vid, const uint8_t *desc, size_t length,
                          uint8_t number, struct ipheth_interface *result);
int ipheth_request_init(struct ipheth_request *, uint8_t command, uint8_t interface);
int ipheth_mac(const uint8_t *, size_t, uint8_t mac[6]);
int ipheth_carrier(const uint8_t *, size_t, bool *up);
/* Each call consumes a complete USB transfer. Views expire on callback return.
 * NCM is validated completely before any frames are delivered. */
int ipheth_receive(enum ipheth_rx_mode, const uint8_t *, size_t,
                   ipheth_frame_fn, void *);
int ipheth_receive_diagnose(enum ipheth_rx_mode, const uint8_t *, size_t,
                           ipheth_frame_fn, void *, struct ipheth_rx_diagnostic *);
/* TX is raw Ethernet in both modes, at its exact length, without tail padding. */
int ipheth_transmit(const uint8_t *, size_t, uint8_t *, size_t);
#endif
