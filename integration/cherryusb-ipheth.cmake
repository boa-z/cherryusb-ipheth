# SPDX-License-Identifier: Apache-2.0
# Include after add_subdirectory(cherryusb-ipheth), then link this OBJECT
# target directly so section-only class registration cannot be discarded.
get_filename_component(_ipheth_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(usbh_ipheth OBJECT "${_ipheth_root}/port/cherryusb/usbh_ipheth.c")
target_link_libraries(usbh_ipheth PUBLIC ipheth)
# Supply CherryUSB include paths/configuration on this target in the parent.
