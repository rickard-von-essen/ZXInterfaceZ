#include "usb_driver.h"
#include "usb_mouse.h"
#include "usb_hid.h"
const struct usb_driver *usb_driver_list[] =
{
    &usb_mouse_driver,
    &usb_hid_driver,
};

const unsigned usb_driver_list_count = sizeof(usb_driver_list)/sizeof(usb_driver_list[0]);