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
    if (!p) return IPHETH_EIO;
    if (pbuf_take(p, frame, (u16_t)length) != ERR_OK) { pbuf_free(p); return IPHETH_EIO; }
    if (n->eth.netif->input(p, n->eth.netif) != ERR_OK) { pbuf_free(p); return IPHETH_EIO; }
    return 0;
}
#ifdef RT_USING_FINSH
#include <finsh.h>
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
