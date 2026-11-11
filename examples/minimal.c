/* SPDX-License-Identifier: Apache-2.0 */
#include "usbh_ipheth.h"
/* Call during application initialization after integrating the class/OS port.
 * The application separately initializes CherryUSB and selects a configuration
 * exposing ff/fd/01. Network users then use ordinary sockets on i0. */
void example_ipheth_init(void)
{
    usbh_ipheth_link_anchor();
}
