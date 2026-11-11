/* SPDX-License-Identifier: Apache-2.0 */
#include <rtthread.h>
#include <rtdevice.h>
#include <netif/ethernetif.h>
#include <lwip/netifapi.h>
#include <lwip/pbuf.h>
#include "usbh_ipheth.h"
#include <string.h>
/* Persistent registered eth_devices avoid dangling tcpip mailbox pointers on
 * unplug. They remain down while detached and are reused on reconnect. */
struct ipheth_net {
    struct eth_device eth; /* first: RT-Thread parent casting */
    struct rt_mutex lock;
    bool initialized, claimed;
    struct usbh_ipheth *usb;
    uint8_t mac[6];
    uint8_t tx[IPHETH_FRAME_MAX];
    uint32_t probe_ms;
    uint32_t probe_tx_errors, probe_rx_errors, probe_usb_errors;
    uint32_t rx_alloc_fail, rx_copy_fail, rx_input_fail;
    int rx_input_result;
};
static struct ipheth_net nets[CONFIG_USBHOST_IPHETH_MAX_DEVICES];
uint32_t usbh_ipheth_now_ms(void)
{
    return (uint32_t)((uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}
static rt_err_t control(rt_device_t dev, int cmd, void *arg)
{
    struct ipheth_net *n = (void *)dev;
    if (cmd != NIOCTL_GADDR || !arg) return -RT_EINVAL;
    memcpy(arg, n->mac, 6);
    return RT_EOK;
}
static rt_err_t transmit(rt_device_t dev, struct pbuf *p)
{
    struct ipheth_net *n = (void *)dev;
    if (!p || p->tot_len > sizeof(n->tx)) return -RT_EINVAL;
    rt_mutex_take(&n->lock, RT_WAITING_FOREVER);
    int rc = IPHETH_ENODEV;
    if (n->usb && pbuf_copy_partial(p, n->tx, p->tot_len, 0) == p->tot_len)
        rc = usbh_ipheth_send(n->usb, n->tx, p->tot_len);
    rt_mutex_release(&n->lock);
    return rc == 0 ? RT_EOK : -RT_ERROR;
}
static struct pbuf *receive_unused(rt_device_t dev) { (void)dev; return NULL; }
#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops ops = {
    .init = NULL, .open = NULL, .close = NULL, .read = NULL, .write = NULL, .control = control
};
#endif
static void reset_address(struct netif *netif)
{
    struct ipheth_net *n = netif->state;
    memcpy(netif->hwaddr, n->mac, 6);
#if LWIP_IPV6
    for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
        netif_ip6_addr_set_state(netif, i, IP6_ADDR_INVALID);
    netif_create_ip6_linklocal_address(netif, 1);
    netif_set_ip6_autoconfig_enabled(netif, 1);
#endif
}
void *usbh_ipheth_net_attach(struct usbh_ipheth *usb, const uint8_t mac[6])
{
    struct ipheth_net *n = NULL;
    rt_base_t f = rt_hw_interrupt_disable();
    for (unsigned i = 0; i < CONFIG_USBHOST_IPHETH_MAX_DEVICES; i++)
        if (!nets[i].claimed) { n = &nets[i]; n->claimed = true; break; }
    rt_hw_interrupt_enable(f);
    if (!n) return NULL;
    if (n->initialized) rt_mutex_take(&n->lock, RT_WAITING_FOREVER);
    memcpy(n->mac, mac, 6);
    if (n->initialized) rt_mutex_release(&n->lock);
    if (!n->initialized) {
        unsigned i = (unsigned)(n - nets);
        char name[3] = { 'i', (char)('0' + i), 0 };
        if (rt_device_find(name)) goto fail;
        if (rt_mutex_init(&n->lock, "ipheth", RT_IPC_FLAG_PRIO) != RT_EOK) goto fail;
#ifdef RT_USING_DEVICE_OPS
        n->eth.parent.ops = &ops;
#else
        n->eth.parent.control = control;
#endif
        n->eth.eth_rx = receive_unused; n->eth.eth_tx = transmit;
        if (eth_device_init(&n->eth, name) != RT_EOK) {
            rt_mutex_detach(&n->lock); goto fail;
        }
        n->initialized = true;
        rt_kprintf("IPHETH network %s registered\n", name);
    }
    /* Address updates run in the lwIP thread; never hold the TX lock across
     * a tcpip callback. The USB pointer is published only after setup. */
    if (netifapi_netif_common(n->eth.netif, reset_address, NULL) != ERR_OK) goto fail;
    rt_mutex_take(&n->lock, RT_WAITING_FOREVER);
    n->usb = usb;
    n->probe_ms = usbh_ipheth_now_ms();
    n->probe_tx_errors = n->probe_rx_errors = n->probe_usb_errors = 0;
    f = rt_hw_interrupt_disable();
    n->rx_alloc_fail = n->rx_copy_fail = n->rx_input_fail = 0;
    n->rx_input_result = 0;
    rt_hw_interrupt_enable(f);
    rt_mutex_release(&n->lock);
    return n;
fail:
    f = rt_hw_interrupt_disable(); n->claimed = false; rt_hw_interrupt_enable(f);
    return NULL;
}
void usbh_ipheth_net_detach(void *net)
{
    struct ipheth_net *n = net;
    rt_mutex_take(&n->lock, RT_WAITING_FOREVER);
    n->usb = NULL; /* waits for every send that could retain the USB pointer */
    rt_mutex_release(&n->lock);
    eth_device_linkchange(&n->eth, RT_FALSE);
    rt_base_t f = rt_hw_interrupt_disable(); n->claimed = false; rt_hw_interrupt_enable(f);
}
void usbh_ipheth_net_link(void *net, bool up)
{
    struct ipheth_net *n = net;
    eth_device_linkchange(&n->eth, up ? RT_TRUE : RT_FALSE);
}
int usbh_ipheth_net_receive(void *net, const uint8_t *frame, size_t length)
{
    struct ipheth_net *n = net;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)length, PBUF_POOL);
    if (!p) {
        rt_base_t f = rt_hw_interrupt_disable(); ++n->rx_alloc_fail; rt_hw_interrupt_enable(f);
        return IPHETH_EIO;
    }
    if (pbuf_take(p, frame, (u16_t)length) != ERR_OK) {
        rt_base_t f = rt_hw_interrupt_disable(); ++n->rx_copy_fail; rt_hw_interrupt_enable(f);
        pbuf_free(p); return IPHETH_EIO;
    }
    err_t rc = n->eth.netif->input(p, n->eth.netif);
    if (rc != ERR_OK) {
        rt_base_t f = rt_hw_interrupt_disable();
        ++n->rx_input_fail; n->rx_input_result = rc;
        rt_hw_interrupt_enable(f);
        pbuf_free(p); return IPHETH_EIO;
    }
    return 0;
}
/* The application calls this from a low-priority worker. No UART output in
 * the USB RX worker, TCP/IP core, ISR, or while holding the network lock. */
static unsigned p95(const struct ipheth_latency *m)
{
    static const unsigned limits[] = {0, 1, 4, 16, 64, 256, 1000};
    uint32_t at = 0, target = (uint32_t)(((uint64_t)m->count * 95 + 99) / 100);
    if (!target) return 0;
    for (unsigned i = 0; i < 8; ++i) {
        at += m->bins[i];
        if (at >= target) return i == 7 ? m->max_ms : limits[i];
    }
    return 0;
}
static void print_latency(const char *name, const struct ipheth_latency *m)
{
    rt_kprintf("[ipheth-time] %s n=%u avg=%u p95_upper=%u max=%u ms\n", name, m->count,
        m->count ? m->total_ms / m->count : 0, p95(m), m->max_ms);
}
static unsigned header16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void print_rx_errors(unsigned i, const struct usbh_ipheth_probe *p)
{
    static const char *const names[] = {
        "ok", "argument", "size", "signature", "header", "block", "table",
        "range", "frame", "overlap", "terminator", "callback"
    };
    for (unsigned r = 1; r < IPHETH_RX_REASON_COUNT; ++r)
        if (p->rx_reasons[r]) rt_kprintf("[ipheth-rxerr] i%u %s=%u\n", i, names[r], p->rx_reasons[r]);
    if (!p->bad_result) return;
    const uint8_t *h = p->bad_header;
    rt_kprintf("[ipheth-rxerr] i%u oversize=%u callback_remaining=%u\n",
        i, p->rx_oversize, p->rx_callback_remaining);
    rt_kprintf("[ipheth-rxerr] last reason=%u rc=%d actual=%u dpe=%u off=%u len=%u\n",
        p->bad.reason, p->bad_result, p->bad_actual, p->bad.index, p->bad.offset, p->bad.length);
    rt_kprintf("[ipheth-rxerr] nth=%02x%02x%02x%02x hlen=%u seq=%u block=%u ndp=%u\n",
        h[0], h[1], h[2], h[3], header16(h+4), header16(h+6), header16(h+8), header16(h+10));
    rt_kprintf("[ipheth-rxerr] ndpsig=%02x%02x%02x%02x table=%u next=%u validated=%u delivered=%u\n",
        h[12], h[13], h[14], h[15], header16(h+16), header16(h+18), p->bad.validated, p->bad.delivered);
}
void ipheth_performance_report(void)
{
    for (unsigned i = 0; i < CONFIG_USBHOST_IPHETH_MAX_DEVICES; ++i) {
        struct ipheth_net *n = &nets[i];
        if (!n->initialized || rt_mutex_take(&n->lock, RT_WAITING_NO) != RT_EOK) continue;
        uint32_t now = usbh_ipheth_now_ms(), dt = now - n->probe_ms;
        if (!n->usb || dt < 5000) { rt_mutex_release(&n->lock); continue; }
        struct usbh_ipheth_probe p;
        usbh_ipheth_take_probe(n->usb, &p);
        unsigned txerr = p.tx_errors - n->probe_tx_errors;
        unsigned rxerr = p.rx_errors - n->probe_rx_errors;
        unsigned usberr = p.usb_errors - n->probe_usb_errors;
        n->probe_ms = now; n->probe_tx_errors = p.tx_errors;
        n->probe_rx_errors = p.rx_errors; n->probe_usb_errors = p.usb_errors;
        rt_base_t flags = rt_hw_interrupt_disable();
        unsigned alloc_fail = n->rx_alloc_fail, copy_fail = n->rx_copy_fail, input_fail = n->rx_input_fail;
        int input_result = n->rx_input_result;
        n->rx_alloc_fail = n->rx_copy_fail = n->rx_input_fail = 0;
        n->rx_input_result = 0;
        rt_hw_interrupt_enable(flags);
        rt_mutex_release(&n->lock);
        rt_kprintf("[ipheth-perf] i%u dt=%ums carrier_poll=%u submit=%u complete=%u\n",
            i, dt, p.carrier_poll, p.rx_submissions, p.rx_completions);
        rt_kprintf("[ipheth-perf] frames=%u bytes=%u errors_rx/tx/usb=%u/%u/%u\n",
            p.rx_frames, p.rx_bytes, rxerr, txerr, usberr);
        print_latency("carrier", &p.carrier); print_latency("wake", &p.rx_wake);
        print_latency("rearm", &p.rx_rearm); print_latency("parse", &p.rx_parse);
        print_latency("tx_lock", &p.tx_lock); print_latency("tx_urb", &p.tx_urb);
        print_rx_errors(i, &p);
        if (alloc_fail || copy_fail || input_fail)
            rt_kprintf("[ipheth-neterr] i%u alloc=%u copy=%u input=%u last_input_rc=%d\n",
                i, alloc_fail, copy_fail, input_fail, input_result);
    }
}
#ifdef RT_USING_FINSH
#include <finsh.h>
/* Only changes periodic carrier queries after the initial query. No forced
 * link-up, shorter timeout, second URB, or lifetime/locking changes. */
static void ipheth_probe(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "carrier") ||
        (strcmp(argv[2], "on") && strcmp(argv[2], "off"))) {
        rt_kprintf("ipheth_probe carrier on|off (diagnostic; reconnect restores on)\n");
        return;
    }
    bool enabled = !strcmp(argv[2], "on");
    for (unsigned i = 0; i < CONFIG_USBHOST_IPHETH_MAX_DEVICES; ++i) {
        struct ipheth_net *n = &nets[i];
        if (!n->initialized) continue;
        rt_mutex_take(&n->lock, RT_WAITING_FOREVER);
        if (n->usb) usbh_ipheth_set_carrier_poll(n->usb, enabled);
        rt_mutex_release(&n->lock);
    }
    rt_kprintf("[ipheth-probe] periodic carrier %s; initial query retained\n", enabled ? "on" : "off");
}
MSH_CMD_EXPORT(ipheth_probe, Apple Ethernet single variable carrier experiment);
static void ipheth_status(void)
{
    for (unsigned i = 0; i < CONFIG_USBHOST_IPHETH_MAX_DEVICES; i++) {
        struct ipheth_net *n = &nets[i];
        if (!n->initialized) continue;
        struct usbh_ipheth_stats s = {0};
        rt_mutex_take(&n->lock, RT_WAITING_FOREVER);
        if (n->usb) usbh_ipheth_get_stats(n->usb, &s);
        rt_kprintf("i%u: %s MAC %02x:%02x:%02x:%02x:%02x:%02x RX=%u TX=%u rxerr=%u txerr=%u usberr=%u\n",
            i, n->usb ? "attached" : "detached", n->mac[0], n->mac[1], n->mac[2],
            n->mac[3], n->mac[4], n->mac[5], s.rx_frames, s.tx_frames,
            s.rx_errors, s.tx_errors, s.usb_errors);
        rt_kprintf("  USB RX: submitted=%u completions=%u bytes=%u control=%u ready=%s carrier=%s deferred=%u\n",
            s.rx_submissions, s.rx_completions, s.rx_bytes, s.rx_control_transfers,
            s.rx_ready ? "yes" : "no", s.carrier_up ? "up" : "down", s.tx_deferred);
        rt_kprintf("  TX failures: frame=%u submit=%u short=%u last=%d length=%u actual=%u\n",
            s.tx_invalid_frames, s.tx_submit_errors, s.tx_short_transfers,
            s.last_tx_result, s.last_tx_length, s.last_tx_actual);
        rt_mutex_release(&n->lock);
    }
}
MSH_CMD_EXPORT(ipheth_status, Apple USB Ethernet counters);
#endif
