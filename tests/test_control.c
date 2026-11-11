/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise upstream's actual EP0 API: HCD length includes the Setup packet. */
#include "usbh_core.h"
#include <assert.h>
#include <stdio.h>
static int raw_length, submit_error, submissions, locked;
int usb_osal_mutex_take(usb_osal_mutex_t m)
{ assert(m && !locked); locked = 1; return 0; }
int usb_osal_mutex_give(usb_osal_mutex_t m)
{ assert(m && locked); locked = 0; return 0; }
int usbh_submit_urb(struct usbh_urb *urb)
{
    assert(locked);
    submissions++;
    urb->actual_length = raw_length;
    return submit_error;
}
static void check(struct usbh_hubport *h, struct usb_setup_packet *s,
                  int raw, int error, int expected, int attempts)
{
    raw_length = raw; submit_error = error; submissions = 0;
    assert(usbh_control_transfer(h, s, NULL) == expected);
    assert(submissions == attempts && !locked);
}
int main(void)
{
    struct usbh_hubport h = {0};
    struct usb_setup_packet s = {0};
    h.connected = true; h.mutex = &h;
    check(&h, &s, 8, 0, 0, 1); /* ENABLE_NCM / SET_INTERFACE */
    s.wLength = 64;
    check(&h, &s, 14, 0, 6, 1); /* short MAC reply */
    check(&h, &s, 72, 0, 64, 1);
    check(&h, &s, 9, 0, 1, 1); /* carrier */
    check(&h, &s, 7, 0, -USB_ERR_IO, 3);
    check(&h, &s, 0, -USB_ERR_STALL, -USB_ERR_STALL, 3);
    check(&h, &s, 0, -USB_ERR_TIMEOUT, -USB_ERR_TIMEOUT, 1);
    h.connected = false;
    check(&h, &s, 0, 0, -USB_ERR_NODEV, 0);
    assert(usbh_control_transfer(NULL, &s, NULL) == -USB_ERR_INVAL);
    assert(usbh_control_transfer(&h, NULL, NULL) == -USB_ERR_INVAL);
    puts("upstream EP0 data-length and error regression passed");
    return 0;
}
