/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "usbh_core.h"
#include "usbh_ipheth.h"
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
extern const struct usbh_class_info ipheth_class_info;
static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint attached, detached, delivered, killed, live_resources;
static atomic_int fail_alloc, fail_thread, reject_attach, fail_cmd, mac_short;
static atomic_int ncm_stall, tx_failure, tx_short, auto_frame;
static atomic_int rx_fault, tx_stall, kill_failure;
static atomic_uint halt_cleared, carrier_requests;
static atomic_int tx_delay, tx_entered;
static atomic_uint expected_tx_length = 60;
static struct usbh_ipheth *bound;
static struct usbh_urb *pending[2];
static void *allocate(size_t n)
{
    int v = atomic_load(&fail_alloc);
    if (v > 0 && atomic_fetch_sub(&fail_alloc, 1) == 1) return NULL;
    void *p = calloc(1, n); assert(p); live_resources++; return p;
}
static void deallocate(void *p) { assert(p); live_resources--; free(p); }
size_t usb_osal_enter_critical_section(void) { pthread_mutex_lock(&critical); return 0; }
void usb_osal_leave_critical_section(size_t f) { (void)f; pthread_mutex_unlock(&critical); }
usb_osal_sem_t usb_osal_sem_create(uint32_t initial)
{ sem_t *s = allocate(sizeof(*s)); if (s) assert(!sem_init(s, 0, initial)); return s; }
void usb_osal_sem_delete(usb_osal_sem_t s) { assert(!sem_destroy(s)); deallocate(s); }
int usb_osal_sem_give(usb_osal_sem_t s) { return sem_post(s); }
int usb_osal_sem_take(usb_osal_sem_t s, uint32_t timeout)
{
    int rc;
    if (timeout == USB_OSAL_WAITING_FOREVER) {
        do { rc = sem_wait(s); } while (rc && errno == EINTR);
    } else {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout / 1000; ts.tv_nsec += (long)(timeout % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        do { rc = sem_timedwait(s, &ts); } while (rc && errno == EINTR);
    }
    return rc ? -USB_ERR_TIMEOUT : 0;
}
void usb_osal_sem_reset(usb_osal_sem_t s) { while (!sem_trywait(s)) {} }
usb_osal_mutex_t usb_osal_mutex_create(void)
{ pthread_mutex_t *m = allocate(sizeof(*m)); if (m) assert(!pthread_mutex_init(m, NULL)); return m; }
void usb_osal_mutex_delete(usb_osal_mutex_t m) { assert(!pthread_mutex_destroy(m)); deallocate(m); }
int usb_osal_mutex_take(usb_osal_mutex_t m) { return pthread_mutex_lock(m); }
int usb_osal_mutex_give(usb_osal_mutex_t m) { return pthread_mutex_unlock(m); }
void usb_osal_msleep(uint32_t ms)
{ struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000 }; nanosleep(&ts, NULL); }
uint32_t usbh_ipheth_now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }
struct launch { usb_thread_entry_t entry; void *arg; };
static void *run(void *arg)
{ struct launch a = *(struct launch *)arg; free(arg); a.entry(a.arg); return NULL; }
usb_osal_thread_t usb_osal_thread_create(const char *n, uint32_t stack, uint32_t prio, usb_thread_entry_t e, void *arg)
{
    (void)n; (void)stack; (void)prio;
    if (atomic_load(&fail_thread)) return NULL;
    struct launch *a = malloc(sizeof(*a)); assert(a); a->entry = e; a->arg = arg;
    pthread_t id; assert(!pthread_create(&id, NULL, run, a)); pthread_detach(id); return (void *)1;
}
void usb_osal_thread_delete(usb_osal_thread_t thread) { assert(!thread); pthread_exit(NULL); }
int usbh_control_transfer(struct usbh_hubport *h, struct usb_setup_packet *s, uint8_t *p)
{
    assert(s != h->setup); assert((uintptr_t)s % CONFIG_USB_ALIGN_SIZE == 0);
    assert(s->wIndex == 7 || s->wIndex == 0x86 || s->wIndex == 5);
    if (s->bmRequestType == 1) { assert(s->bRequest == USB_REQUEST_SET_INTERFACE); return 0; }
    if (s->bmRequestType == 2) { halt_cleared++; return 0; }
    if (atomic_load(&fail_cmd) == s->bRequest) return -USB_ERR_IO;
    if (s->bRequest == IPHETH_CMD_NCM) {
        assert(s->bmRequestType == 0x41 && !s->wLength && !p);
        return atomic_load(&ncm_stall) ? -USB_ERR_STALL : 0;
    }
    assert(s->bmRequestType == 0xc0 && s->wLength == 64 && p);
    assert((uintptr_t)p % CONFIG_USB_ALIGN_SIZE == 0);
    if (s->bRequest == IPHETH_CMD_MAC) {
        uint8_t mac[] = {2,3,4,5,6,7}; memcpy(p, mac, 6);
        return atomic_load(&mac_short) ? 5 : 6;
    }
    assert(s->bRequest == IPHETH_CMD_CARRIER); carrier_requests++; p[0] = 4; return 1;
}
static void put16(uint8_t *p, unsigned n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
int usbh_submit_urb(struct usbh_urb *u)
{
    assert((uintptr_t)u->transfer_buffer % CONFIG_USB_ALIGN_SIZE == 0);
    if (u->ep->bEndpointAddress == 5) {
        assert(u->timeout == 1000 && !u->complete);
        assert(u->transfer_buffer_length == atomic_load(&expected_tx_length));
        if (atomic_load(&tx_delay)) { atomic_store(&tx_entered, 1); usb_osal_msleep(80); }
        if (atomic_load(&tx_failure)) return -USB_ERR_IO;
        if (atomic_exchange(&tx_stall, 0)) return -USB_ERR_STALL;
        u->actual_length = u->transfer_buffer_length - (atomic_load(&tx_short) ? 1 : 0);
        return 0;
    }
    assert(u->ep->bEndpointAddress == 0x86 && !u->timeout && u->complete);
    if (atomic_exchange(&rx_fault, 0)) { u->complete(u->arg, -USB_ERR_STALL); }
    else if (atomic_exchange(&auto_frame, 0)) {
        uint8_t *p = u->transfer_buffer; memset(p, 0, 122);
        unsigned size;
        if (u->transfer_buffer_length == 65536) {
            memcpy(p, "NCMH", 4); put16(p+4, 12); put16(p+8, 122); put16(p+10, 12);
            memcpy(p+12, "NCM0", 4); put16(p+16, 96); put16(p+20, 108); put16(p+22, 14);
            size = 122;
        } else size = 16;
        u->complete(u->arg, (int)size);
    } else {
        size_t f = usb_osal_enter_critical_section();
        unsigned i; for (i = 0; i < 2 && pending[i]; i++) {}
        assert(i < 2); pending[i] = u; u->hcpriv = u;
        usb_osal_leave_critical_section(f);
    }
    return 0;
}
int usbh_kill_urb(struct usbh_urb *u)
{
    size_t f = usb_osal_enter_critical_section();
    unsigned i; for (i = 0; i < 2 && pending[i] != u; i++) {}
    assert(i < 2);
    if (atomic_load(&kill_failure)) { usb_osal_leave_critical_section(f); return -USB_ERR_TIMEOUT; }
    pending[i] = NULL; u->hcpriv = NULL; killed++;
    usb_osal_leave_critical_section(f); return 0;
}
void *usbh_ipheth_net_attach(struct usbh_ipheth *d, const uint8_t mac[6])
{ assert(mac[0] == 2); if (atomic_load(&reject_attach)) return NULL; attached++; bound = d; return d; }
void usbh_ipheth_net_detach(void *n) { assert(n); detached++; }
int usbh_ipheth_net_receive(void *n, const uint8_t *p, size_t len)
{ assert(n && p && len == 14); delivered++; return 0; }
void usbh_ipheth_net_link(void *n, bool up) { assert(n); (void)up; }
static uint8_t desc[] = {
    9,2, 50,0, 1,4,0,0x80,50,
    9,4,7,0,0,0xff,0xfd,1,0,
    9,4,7,1,2,0xff,0xfd,1,0,
    7,5,0x86,2,0,2,0, 7,5,5,2,0,2,0,
    9,4,7,2,0,0xff,0xfd,1,0
};
static void init(struct usbh_hubport *h)
{
    memset(h, 0, sizeof(*h)); h->connected = true;
    h->device_desc.idVendor = 0x05ac; h->device_desc.idProduct = 0xffff;
    h->raw_config_desc = desc; h->config.config_desc.wTotalLength = sizeof(desc);
    put16(desc+2, sizeof(desc));
    h->config.intf[2].altsetting[0].intf_desc.bInterfaceNumber = 7;
    h->config.intf[1].priv = (void *)0x1234; /* independent USBMUX sentinel */
    atomic_store(&fail_cmd, -1);
}
static void expect_failure(struct usbh_hubport *h)
{
    assert(ipheth_class_info.class_driver->connect(h, 2) == 0);
    assert(!h->config.intf[2].priv && h->config.intf[1].priv == (void *)0x1234);
    assert(atomic_load(&live_resources) == 0);
}
static void *concurrent_send(void *arg)
{
    uint8_t packet[60] = {0};
    assert(!usbh_ipheth_send(arg, packet, sizeof(packet)));
    return NULL;
}
static void wait_link(void)
{
    for (unsigned i = 0; i < 500; i++) {
        struct usbh_ipheth_stats s; usbh_ipheth_get_stats(bound, &s);
        if (s.carrier_up) return;
        usb_osal_msleep(1);
    }
    assert(!"carrier did not become ready");
}
static void complete_pending(int length)
{
    struct usbh_urb *u = NULL;
    for (unsigned retry = 0; retry < 500 && !u; retry++) {
        size_t f = usb_osal_enter_critical_section();
        for (unsigned i = 0; i < 2; i++) if (pending[i]) {
            u = pending[i]; pending[i] = NULL; u->hcpriv = NULL; break;
        }
        usb_osal_leave_critical_section(f);
        if (!u) usb_osal_msleep(1);
    }
    assert(u);
    if (length == 4) { u->transfer_buffer[0] = 0; u->transfer_buffer[1] = 1; }
    u->complete(u->arg, length);
}
int main(void)
{
    const struct usbh_class_driver *c = ipheth_class_info.class_driver;
    assert(ipheth_class_info.bInterfaceClass == 0xff && ipheth_class_info.bInterfaceSubClass == 0xfd);
    assert(ipheth_class_info.bInterfaceProtocol == 1);
    struct usbh_hubport h; init(&h);
    for (int i = 1; i <= 3; i++) { atomic_store(&fail_alloc, i); expect_failure(&h); }
    atomic_store(&fail_alloc, 0);
    atomic_store(&fail_cmd, IPHETH_CMD_MAC); expect_failure(&h);
    atomic_store(&fail_cmd, IPHETH_CMD_NCM); expect_failure(&h);
    atomic_store(&fail_cmd, -1); atomic_store(&mac_short, 1); expect_failure(&h);
    atomic_store(&mac_short, 0); atomic_store(&reject_attach, 1); expect_failure(&h);
    atomic_store(&reject_attach, 0); atomic_store(&fail_thread, 1); expect_failure(&h);
    atomic_store(&fail_thread, 0);
    h.device_desc.idVendor = 0x1234; assert(c->connect(&h, 2) < 0); h.device_desc.idVendor = 0x05ac;
    for (unsigned i = 0; i < 20; i++) {
        unsigned before = atomic_load(&delivered);
        atomic_store(&ncm_stall, i & 1); atomic_store(&auto_frame, 1);
        assert(!c->connect(&h, 2));
        for (unsigned wait = 0; wait < 100 && atomic_load(&delivered) == before; wait++) usb_osal_msleep(1);
        assert(atomic_load(&delivered) == before + 1);
        wait_link();
        uint8_t packet[60] = {0};
        atomic_store(&expected_tx_length, 42);
        assert(!usbh_ipheth_send(bound, packet, 42));
        atomic_store(&expected_tx_length, 60);
        atomic_store(&tx_failure, 1); assert(usbh_ipheth_send(bound, packet, sizeof(packet)) < 0);
        atomic_store(&tx_failure, 0); atomic_store(&tx_short, 1); assert(usbh_ipheth_send(bound, packet, sizeof(packet)) < 0);
        atomic_store(&tx_short, 0);
        struct usbh_ipheth_stats s; usbh_ipheth_get_stats(bound, &s);
        assert(s.rx_frames == 1 && s.tx_frames == 1 && s.tx_errors == 2);
        assert(s.tx_submit_errors == 1 && s.tx_short_transfers == 1 && !s.tx_invalid_frames);
        assert(s.last_tx_result == IPHETH_EIO && s.last_tx_length == 60 && s.last_tx_actual == 59);
        assert(usbh_ipheth_send(bound, packet, 13) == IPHETH_EINVAL);
        usbh_ipheth_get_stats(bound, &s);
        assert(s.tx_errors == 3 && s.tx_invalid_frames == 1 && s.last_tx_length == 13);
        assert(!c->disconnect(&h, 2)); assert(!c->disconnect(&h, 2));
        assert(!h.config.intf[2].priv && h.config.intf[1].priv == (void *)0x1234);
        assert(!atomic_load(&live_resources));
    }
    assert(atomic_load(&killed) == 20);
    /* STALL recovery uses the worker's private EP0 and resets the toggle. */
    atomic_store(&rx_fault, 1); atomic_store(&auto_frame, 1);
    assert(!c->connect(&h, 2));
    for (unsigned i = 0; i < 200 && !atomic_load(&halt_cleared); i++) usb_osal_msleep(1);
    assert(atomic_load(&halt_cleared) == 1);
    wait_link();
    atomic_store(&tx_stall, 1); uint8_t packet[60] = {0};
    assert(usbh_ipheth_send(bound, packet, sizeof(packet)) < 0);
    for (unsigned i = 0; i < 300 && atomic_load(&halt_cleared) < 2; i++) usb_osal_msleep(1);
    assert(atomic_load(&halt_cleared) == 2);
    assert(!usbh_ipheth_send(bound, packet, sizeof(packet)));
    assert(!c->disconnect(&h, 2));
    /* Disconnect must drain an already running synchronous TX before any
     * mutex/URB storage can be released. */
    atomic_store(&auto_frame, 1);
    assert(!c->connect(&h, 2)); wait_link();
    atomic_store(&tx_delay, 1);
    pthread_t sender; assert(!pthread_create(&sender, NULL, concurrent_send, bound));
    for (unsigned i = 0; i < 200 && !atomic_load(&tx_entered); i++) usb_osal_msleep(1);
    assert(atomic_load(&tx_entered));
    assert(!c->disconnect(&h, 2)); assert(!pthread_join(sender, NULL));
    atomic_store(&tx_delay, 0);
    assert(!atomic_load(&live_resources));
    /* Carrier control alone must not authorize TX before Bulk IN is ready.
     * Zero-length and Apple control completions unlock readiness without
     * being misparsed/delivered as Ethernet frames. Reconnect resets it. */
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        unsigned polls = atomic_load(&carrier_requests), frames = atomic_load(&delivered);
        assert(!c->connect(&h, 2)); usb_osal_msleep(10);
        struct usbh_ipheth_stats s; usbh_ipheth_get_stats(bound, &s);
        assert(!s.rx_ready && !s.carrier_up && !s.rx_completions);
        assert(atomic_load(&carrier_requests) == polls);
        assert(usbh_ipheth_send(bound, packet, sizeof(packet)) == IPHETH_ENODEV);
        usbh_ipheth_get_stats(bound, &s);
        assert(s.tx_deferred == 1 && !s.tx_errors && !s.tx_frames);
        complete_pending(cycle ? 4 : 0); wait_link();
        usbh_ipheth_get_stats(bound, &s);
        assert(s.rx_ready && s.rx_completions == 1 && s.rx_control_transfers == 1);
        assert(!s.rx_errors && atomic_load(&delivered) == frames);
        assert(!usbh_ipheth_send(bound, packet, sizeof(packet)));
        assert(!c->disconnect(&h, 2)); assert(!atomic_load(&live_resources));
    }
    /* Distinct hports have independent slots/URBs; exhaustion is harmless. */
    struct usbh_hubport h2, h3; init(&h2); init(&h3);
    assert(!c->connect(&h, 2)); assert(!c->connect(&h2, 2));
    unsigned allocated = atomic_load(&live_resources);
    assert(c->connect(&h3, 2) == 0 && !h3.config.intf[2].priv);
    assert(atomic_load(&live_resources) == allocated);
    assert(!c->disconnect(&h, 2)); assert(!c->disconnect(&h2, 2));
    assert(!atomic_load(&live_resources));
    /* Failed cancellation quarantines all DMA/callback state. A late callback
     * remains safe and cannot deliver into a detached network adapter. */
    assert(!c->connect(&h, 2));
    usb_osal_msleep(10);
    atomic_store(&kill_failure, 1);
    unsigned received = atomic_load(&delivered);
    assert(!c->disconnect(&h, 2));
    assert(atomic_load(&live_resources) == 3);
    size_t f = usb_osal_enter_critical_section();
    struct usbh_urb *late = pending[0] ? pending[0] : pending[1];
    usb_osal_leave_critical_section(f);
    assert(late); late->complete(late->arg, 122);
    assert(atomic_load(&delivered) == received);
    atomic_store(&kill_failure, 0);
    assert(atomic_load(&attached) == atomic_load(&detached));
    puts("class tests passed: unwind, NCM/legacy, TX errors, 20 reconnects, STALL, multi-device, failed-cancel quarantine");
}
