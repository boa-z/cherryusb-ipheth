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
    uint32_t rx_reasons[IPHETH_RX_REASON_COUNT], rx_oversize, rx_callback_remaining;
    uint32_t bad_actual;
    int bad_result;
    struct ipheth_rx_diagnostic bad;
    /* Only NTH16/NDP16 headers, never Ethernet/TLS payload. */
    uint8_t bad_header[20];
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
    /* External host-session evidence is separate from observed RX readiness.
     * It permits a real Carrier query, never fabricates a completion/link. */
    bool host_ready, rx_armed;
    /* Effective network link and explicit external USB-session policy are
     * independent of the observed vendor Carrier reply. */
    bool link_up, session_link;
    uint32_t carrier_queries;
    int last_carrier_result;
    /* Bounded vendor-status bytes only; cleared on failed/invalid replies.
     * Observed reply is distinct from the policy-controlled effective link. */
    uint8_t carrier_reply[8], carrier_reply_length;
    bool carrier_reply_up;
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
struct usbh_hubport;
/* Optional host policy after successful external session activation. The
 * caller must bind the notification to its current USB attachment epoch.
 * Default false preserves Linux-style first-RX activation. Reset on detach. */
void usbh_ipheth_host_ready(struct usbh_hubport *, bool ready);
/* Opt-in alternative for an externally established USB network session.
 * Unlike the polling hint above, this authorizes link/TX without tethering
 * Carrier. Requires a submitted RX and live session; never fabricates RX or
 * Carrier. The host must revoke it before closing its session, and bind it
 * to the attachment epoch. Default false; reset on disconnect/reconnect. */
void usbh_ipheth_session_link(struct usbh_hubport *, bool ready);
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
