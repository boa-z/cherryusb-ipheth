/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USBH_IPHETH_H
#define USBH_IPHETH_H
#include "ipheth.h"
#ifndef CONFIG_USBHOST_IPHETH_MAX_DEVICES
#define CONFIG_USBHOST_IPHETH_MAX_DEVICES 2
#endif
struct usbh_ipheth;
/* Windowed elapsed-time histograms, drained by one low-priority reporter.
 * Bins: <=0, <=1, <=4, <=16, <=64, <=256, <=1000, >1000 ms. */
struct ipheth_latency {
    uint32_t count, total_ms, max_ms, bins[8];
};
static inline void ipheth_latency_add(struct ipheth_latency *s, uint32_t ms)
{
    static const uint32_t limits[] = {0, 1, 4, 16, 64, 256, 1000};
    unsigned i = 0;
    while (i < 7 && ms > limits[i]) ++i;
    ++s->count; s->total_ms += ms; ++s->bins[i];
    if (ms > s->max_ms) s->max_ms = ms;
}
struct usbh_ipheth_probe {
    struct ipheth_latency carrier, rx_wake, rx_rearm, rx_parse, tx_lock, tx_urb;
    uint32_t carrier_calls, rx_submissions, rx_completions, rx_bytes, rx_frames;
    uint32_t tx_errors, rx_errors, usb_errors;
    bool carrier_poll;
};
struct usbh_ipheth_stats {
    uint32_t rx_frames, tx_frames, rx_errors, tx_errors, usb_errors;
    /* TX diagnostics distinguish framing rejection, HCD failure and short OUT.
     * Last fields describe the last failed attempt and persist after success.
     * last_tx_result is IPHETH_EINVAL/EIO or the negative HCD result. */
    uint32_t tx_invalid_frames, tx_submit_errors, tx_short_transfers;
    int last_tx_result;
    uint32_t last_tx_length, last_tx_actual;
    uint32_t rx_submissions, rx_completions, rx_bytes, rx_control_transfers, tx_deferred;
    /* A successful Bulk IN completion (including empty/control transfers)
     * establishes readiness, following Linux ipheth; no pairing protocol. */
    bool rx_ready, carrier_up;
};
/* Callbacks run in thread context. attach/detach run in the hub thread;
 * receive/link run in the device worker. No callback may retain frame views.
 * The adapter must serialize send against detach; no access after detach.
 * A NULL attach result rejects the binding and unwinds the alternate setting. */
void *usbh_ipheth_net_attach(struct usbh_ipheth *, const uint8_t mac[6]);
void usbh_ipheth_net_detach(void *);
int usbh_ipheth_net_receive(void *, const uint8_t *, size_t);
void usbh_ipheth_net_link(void *, bool);
int usbh_ipheth_send(struct usbh_ipheth *, const uint8_t *, size_t);
void usbh_ipheth_get_stats(struct usbh_ipheth *, struct usbh_ipheth_stats *);
/* No URB/buffer ownership changes. Disabling polling retains the initial
 * carrier query and last known link; diagnostic only, restored on reconnect. */
void usbh_ipheth_set_carrier_poll(struct usbh_ipheth *, bool enabled);
void usbh_ipheth_take_probe(struct usbh_ipheth *, struct usbh_ipheth_probe *);
/* Reference this from the application when using a static archive, or link
 * this class object directly / whole-archive. KEEP(.usbh_class_info) required. */
void usbh_ipheth_link_anchor(void);
/* Monotonic wrapping millisecond clock supplied by the OS port. */
uint32_t usbh_ipheth_now_ms(void);
#endif
