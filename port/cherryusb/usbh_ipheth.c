/* SPDX-License-Identifier: Apache-2.0 */
#include "usbh_core.h"
#include "usbh_ipheth.h"
#include <string.h>
#ifndef CONFIG_USBHOST_IPHETH_THREAD_PRIO
#define CONFIG_USBHOST_IPHETH_THREAD_PRIO 12
#endif
#ifndef CONFIG_USBHOST_IPHETH_THREAD_STACK
#define CONFIG_USBHOST_IPHETH_THREAD_STACK 4096
#endif
/* Private DMA storage: never aliases USBMUX, EP0 scratch space, or pbufs.
 * Every buffer occupies complete cache lines. The HCD owns cache maintenance.
 * Platforms with DMA address constraints may override USB_NOCACHE_RAM_SECTION. */
struct ipheth_dma {
    USB_MEM_ALIGNX uint8_t rx[USB_ALIGN_UP(IPHETH_RX_NCM_MAX, CONFIG_USB_ALIGN_SIZE)];
    USB_MEM_ALIGNX uint8_t tx[USB_ALIGN_UP(IPHETH_FRAME_MAX, CONFIG_USB_ALIGN_SIZE)];
    USB_MEM_ALIGNX uint8_t ctrl[USB_ALIGN_UP(IPHETH_CTRL_SIZE, CONFIG_USB_ALIGN_SIZE)];
    USB_MEM_ALIGNX uint8_t setup[USB_ALIGN_UP(sizeof(struct usb_setup_packet), CONFIG_USB_ALIGN_SIZE)];
};
static USB_NOCACHE_RAM_SECTION struct ipheth_dma dma[CONFIG_USBHOST_IPHETH_MAX_DEVICES];
struct usbh_ipheth {
    bool used, stopping, quarantined, tx_halted;
    struct usbh_hubport *hport;
    struct ipheth_interface intf;
    struct usb_endpoint_descriptor in, out;
    struct usbh_urb rx, tx;
    struct ipheth_dma *dma;
    usb_osal_sem_t ready, exited;
    usb_osal_mutex_t tx_lock;
    enum ipheth_rx_mode mode;
    int rx_result;
    void *net;
    struct usbh_ipheth_stats stats;
};
static struct usbh_ipheth devices[CONFIG_USBHOST_IPHETH_MAX_DEVICES];
static void count(uint32_t *counter)
{
    size_t f = usb_osal_enter_critical_section();
    ++*counter;
    usb_osal_leave_critical_section(f);
}
static bool stopping(struct usbh_ipheth *d)
{
    size_t f = usb_osal_enter_critical_section();
    bool stop = d->stopping;
    usb_osal_leave_critical_section(f);
    return stop;
}
static void stop_device(struct usbh_ipheth *d)
{
    size_t f = usb_osal_enter_critical_section();
    d->stopping = true;
    usb_osal_leave_critical_section(f);
}
static int request(struct usbh_ipheth *d, uint8_t cmd)
{
    struct ipheth_request r;
    struct usb_setup_packet *s = (void *)d->dma->setup;
    ipheth_request_init(&r, cmd, d->intf.number);
    s->bmRequestType = r.type; s->bRequest = r.request;
    s->wValue = r.value; s->wIndex = r.index; s->wLength = r.length;
    /* usbh_control_transfer serializes EP0. Our setup is private and remains
     * immutable during transfer, avoiding hport->setup races with USBMUX. */
    return usbh_control_transfer(d->hport, s, r.length ? d->dma->ctrl : NULL);
}
static int set_alt(struct usbh_ipheth *d, uint8_t alt)
{
    struct usb_setup_packet *s = (void *)d->dma->setup;
    memset(s, 0, sizeof(*s));
    s->bmRequestType = 1; s->bRequest = USB_REQUEST_SET_INTERFACE;
    s->wValue = alt; s->wIndex = d->intf.number;
    return usbh_control_transfer(d->hport, s, NULL);
}
static int clear_halt(struct usbh_ipheth *d, struct usb_endpoint_descriptor *ep)
{
    struct usb_setup_packet *s = (void *)d->dma->setup;
    memset(s, 0, sizeof(*s));
    s->bmRequestType = 2; s->bRequest = USB_REQUEST_CLEAR_FEATURE;
    s->wIndex = ep->bEndpointAddress;
    return usbh_control_transfer(d->hport, s, NULL);
}
static void completed(void *arg, int n)
{
    struct usbh_ipheth *d = arg;
    d->rx_result = n;
    usb_osal_sem_give(d->ready);
}
static int frame(void *arg, const uint8_t *p, size_t n)
{
    struct usbh_ipheth *d = arg;
    int rc = usbh_ipheth_net_receive(d->net, p, n);
    if (!rc) count(&d->stats.rx_frames);
    return rc;
}
static void worker(void *arg)
{
    struct usbh_ipheth *d = arg;
    uint32_t last_carrier = usbh_ipheth_now_ms() - 1000;
    bool pending = false, linked = false;
    while (!stopping(d)) {
        /* Only the worker owns EP0 setup/control storage. TX only raises a
         * recovery flag, so control requests cannot race a receive poll. */
        usb_osal_mutex_take(d->tx_lock);
        if (d->tx_halted && !stopping(d) && clear_halt(d, &d->out) == 0) {
            d->tx.data_toggle = 0;
            d->tx_halted = false;
        }
        usb_osal_mutex_give(d->tx_lock);
        if ((uint32_t)(usbh_ipheth_now_ms() - last_carrier) >= 1000) {
            bool up = false;
            int rc = request(d, IPHETH_CMD_CARRIER);
            if (rc > 0) ipheth_carrier(d->dma->ctrl, (size_t)rc, &up);
            else count(&d->stats.usb_errors);
            if (up != linked) { linked = up; usbh_ipheth_net_link(d->net, up); }
            last_carrier = usbh_ipheth_now_ms();
        }
        if (!pending) {
            usb_osal_sem_reset(d->ready);
            usbh_bulk_urb_fill(&d->rx, d->hport, &d->in, d->dma->rx,
                d->mode == IPHETH_NCM ? IPHETH_RX_NCM_MAX : IPHETH_RX_LEGACY_MAX,
                0, completed, d);
            if (usbh_submit_urb(&d->rx) < 0) {
                count(&d->stats.usb_errors);
                usb_osal_msleep(100);
                continue;
            }
            pending = true;
        }
        if (usb_osal_sem_take(d->ready, 100) == 0) {
            pending = false;
            int rc = d->rx_result;
            if (rc > 0 && !stopping(d)) {
                if (ipheth_receive(d->mode, d->dma->rx, (size_t)rc, frame, d) < 0)
                    count(&d->stats.rx_errors);
            } else if (rc < 0) {
                count(&d->stats.usb_errors);
                /* Includes STALL: clear halt and reset the endpoint toggle.
                 * Use private setup storage, never a shared scratch packet. */
                if (rc == -USB_ERR_STALL && !stopping(d) && clear_halt(d, &d->in) == 0)
                    d->rx.data_toggle = 0;
                usb_osal_msleep(20);
            }
        }
    }
    /* HCD kill must synchronously quiesce DMA and callbacks. Buffers, URBs,
     * and semaphores outlive this call. No forced thread deletion. */
    if (pending && usbh_kill_urb(&d->rx) < 0) {
        /* A failed HCD cancellation cannot prove DMA/callback quiescence.
         * Keep this slot and its synchronization objects permanently alive;
         * never recycle memory that hardware might still reference. */
        d->quarantined = true;
        USB_LOG_ERR("IPHETH: HCD cancellation failed; slot quarantined\r\n");
    }
    usbh_ipheth_net_link(d->net, false);
    usb_osal_sem_give(d->exited); /* last access to device storage */
    usb_osal_thread_delete(NULL);
}
int usbh_ipheth_send(struct usbh_ipheth *d, const uint8_t *p, size_t n)
{
    if (!d) return IPHETH_ENODEV;
    usb_osal_mutex_take(d->tx_lock);
    int rc = IPHETH_ENODEV;
    if (!stopping(d) && !d->tx_halted) {
        unsigned actual = 0;
        bool invalid = false, submit_error = false, short_transfer = false;
        rc = ipheth_transmit(p, n, d->dma->tx, sizeof(d->dma->tx));
        if (rc > 0) {
            unsigned length = (unsigned)rc;
            usbh_bulk_urb_fill(&d->tx, d->hport, &d->out, d->dma->tx,
                               length, 1000, NULL, NULL);
            rc = usbh_submit_urb(&d->tx);
            actual = d->tx.actual_length;
            submit_error = rc < 0;
            if (rc == -USB_ERR_STALL) d->tx_halted = true;
            if (!rc && actual != length) {
                rc = IPHETH_EIO;
                short_transfer = true;
            }
        } else {
            invalid = true;
        }
        size_t f = usb_osal_enter_critical_section();
        if (!rc) ++d->stats.tx_frames;
        if (rc < 0) {
            ++d->stats.tx_errors;
            d->stats.tx_invalid_frames += invalid;
            d->stats.tx_submit_errors += submit_error;
            d->stats.tx_short_transfers += short_transfer;
            d->stats.last_tx_result = rc;
            d->stats.last_tx_length = n > UINT32_MAX ? UINT32_MAX : (uint32_t)n;
            d->stats.last_tx_actual = actual;
        }
        usb_osal_leave_critical_section(f);
    }
    usb_osal_mutex_give(d->tx_lock);
    return rc;
}
void usbh_ipheth_get_stats(struct usbh_ipheth *d, struct usbh_ipheth_stats *s)
{
    if (!d || !s) return;
    size_t f = usb_osal_enter_critical_section();
    *s = d->stats;
    usb_osal_leave_critical_section(f);
}
static void release(struct usbh_ipheth *d)
{
    if (d->tx_lock) usb_osal_mutex_delete(d->tx_lock);
    if (d->ready) usb_osal_sem_delete(d->ready);
    if (d->exited) usb_osal_sem_delete(d->exited);
    size_t f = usb_osal_enter_critical_section();
    d->used = false;
    usb_osal_leave_critical_section(f);
}
static int connect(struct usbh_hubport *h, uint8_t slot)
{
    struct ipheth_interface intf;
    if (slot >= CONFIG_USBHOST_MAX_INTERFACES) return -USB_ERR_INVAL;
    uint8_t number = h->config.intf[slot].altsetting[0].intf_desc.bInterfaceNumber;
    if (ipheth_find_interface(h->device_desc.idVendor, h->raw_config_desc,
                              h->config.config_desc.wTotalLength, number, &intf))
        return -USB_ERR_INVAL;
    struct usbh_ipheth *d = NULL;
    size_t f = usb_osal_enter_critical_section();
    for (unsigned i = 0; i < CONFIG_USBHOST_IPHETH_MAX_DEVICES; i++) {
        if (!devices[i].used) {
            d = &devices[i]; memset(d, 0, sizeof(*d));
            d->used = true; d->dma = &dma[i]; break;
        }
    }
    usb_osal_leave_critical_section(f);
    if (!d) {
        USB_LOG_ERR("IPHETH inactive: no device slot; other interfaces retained\r\n");
        return 0;
    }
    d->hport = h; d->intf = intf;
    d->tx_lock = usb_osal_mutex_create();
    d->ready = usb_osal_sem_create(0); d->exited = usb_osal_sem_create(0);
    int rc = -USB_ERR_NOMEM;
    bool selected = false;
    const char *step = "OSAL allocation";
    if (!d->tx_lock || !d->ready || !d->exited) goto fail;
    step = "SET_INTERFACE";
    rc = set_alt(d, intf.alternate);
    if (rc < 0) goto fail;
    selected = true;
    uint8_t mac[6];
    step = "GET_MAC";
    rc = request(d, IPHETH_CMD_MAC);
    USB_LOG_INFO("IPHETH GET_MAC returned %d data bytes\r\n", rc);
    if (rc < 0) goto fail;
    step = "MAC validation";
    if (ipheth_mac(d->dma->ctrl, (size_t)rc, mac)) { rc = -USB_ERR_INVAL; goto fail; }
    step = "ENABLE_NCM";
    rc = request(d, IPHETH_CMD_NCM);
    USB_LOG_INFO("IPHETH ENABLE_NCM returned %d (success must be 0)\r\n", rc);
    if (rc == 0) d->mode = IPHETH_NCM;
    else if (rc == -USB_ERR_STALL) d->mode = IPHETH_LEGACY;
    else goto fail;
    memset(&d->in, 0, sizeof(d->in)); memset(&d->out, 0, sizeof(d->out));
    d->in.bLength = d->out.bLength = 7;
    d->in.bDescriptorType = d->out.bDescriptorType = USB_DESCRIPTOR_TYPE_ENDPOINT;
    d->in.bEndpointAddress = intf.in; d->out.bEndpointAddress = intf.out;
    d->in.bmAttributes = d->out.bmAttributes = 2;
    d->in.wMaxPacketSize = intf.in_mps; d->out.wMaxPacketSize = intf.out_mps;
    step = "network registration";
    d->net = usbh_ipheth_net_attach(d, mac);
    if (!d->net) { rc = -USB_ERR_NOMEM; goto fail; }
    h->config.intf[slot].priv = d;
    step = "worker creation";
    if (!usb_osal_thread_create("ipheth", CONFIG_USBHOST_IPHETH_THREAD_STACK,
          CONFIG_USBHOST_IPHETH_THREAD_PRIO, worker, d)) {
        h->config.intf[slot].priv = NULL;
        usbh_ipheth_net_detach(d->net); rc = -USB_ERR_NOMEM; goto fail;
    }
    USB_LOG_INFO("IPHETH intf %u alt %u IN %02x OUT %02x mode %s\r\n",
        number, intf.alternate, intf.in, intf.out, d->mode == IPHETH_NCM ? "NCM RX" : "legacy RX");
    return 0;
fail:
    USB_LOG_ERR("IPHETH inactive: %s failed (%d); other interfaces retained\r\n", step, rc);
    (void)step;
    if (selected && h->connected) set_alt(d, 0);
    release(d);
    /* CherryUSB rolls back the whole composite device on a negative connect
     * result. A validated but unavailable optional Ethernet function must
     * not disconnect USBMUX or prevent external pairing/trust. No private
     * object remains; disconnect is a no-op and replug retries initialization. */
    return 0;
}
static int disconnect(struct usbh_hubport *h, uint8_t slot)
{
    struct usbh_ipheth *d = h->config.intf[slot].priv;
    if (!d) return 0;
    stop_device(d);
    /* Worker exits on its bounded 100 ms wait or bounded EP0 operation.
     * Hport, DMA and callbacks remain alive until the acknowledgment. */
    usb_osal_sem_take(d->exited, USB_OSAL_WAITING_FOREVER);
    usbh_ipheth_net_detach(d->net); /* drains in-flight network TX users */
    usb_osal_mutex_take(d->tx_lock);
    usb_osal_mutex_give(d->tx_lock);
    h->config.intf[slot].priv = NULL;
    if (!d->quarantined) release(d);
    return 0;
}
static const struct usbh_class_driver driver = {
    .driver_name = "ipheth", .connect = connect, .disconnect = disconnect
};
/* The class table has a combined VID/PID match. Match the exact
 * interface triplet here, then validate Apple VID in connect (all PIDs). */
CLASS_INFO_DEFINE const struct usbh_class_info ipheth_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .bInterfaceClass = 0xff, .bInterfaceSubClass = 0xfd, .bInterfaceProtocol = 1,
    .id_table = NULL, .class_driver = &driver
};
void usbh_ipheth_link_anchor(void) {}
#ifndef IPHETH_EXTERNAL_NET_ADAPTER
__WEAK void *usbh_ipheth_net_attach(struct usbh_ipheth *d, const uint8_t mac[6])
{ (void)d; (void)mac; return NULL; }
__WEAK void usbh_ipheth_net_detach(void *n) { (void)n; }
__WEAK int usbh_ipheth_net_receive(void *n, const uint8_t *p, size_t len)
{ (void)n; (void)p; (void)len; return IPHETH_EIO; }
__WEAK void usbh_ipheth_net_link(void *n, bool up) { (void)n; (void)up; }
#endif
