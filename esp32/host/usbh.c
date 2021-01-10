#include "dump.h"
#include "esp_log.h"
#include "defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "usbh.h"
#include "usb_driver.h"
#include "usb_descriptor.h"
#include <string.h>
#include "log.h"
#include "esp_system.h"
#include "systemevent.h"
#include "usbhub.h"

#define USBHTAG "USBH"
#define USBHDEBUG(x...) LOG_DEBUG(DEBUG_ZONE_USBH, USBHTAG ,x)

static xQueueHandle usb_cmd_queue = NULL;
static xQueueHandle usb_cplt_queue = NULL;

struct usbcmd
{
    int cmd;
    void *data;
};

struct usbresponse
{
    int status;
    void *data;
};

static struct usb_hub root_hub = { 0 };

#define USB_CMD_INTERRUPT 0
#define USB_CMD_SUBMITREQUEST 1

#define USB_STAT_ATTACHED 1
#define USB_REQUEST_COMPLETED_OK 2
#define USB_REQUEST_COMPLETED_FAIL 3




static void usbh__submit_request_to_ll(struct usb_request *req);
static int usbh__control_request_completed_reply(uint8_t chan, uint8_t stat, struct usb_request *req);
static int usbh__request_completed_reply(uint8_t chan, uint8_t stat, void *req);
static int usbh__issue_setup_data_request(struct usb_request *req);

void usb_ll__connected_callback()
{
    struct usbresponse resp;

    // Connected device.
    ESP_LOGI(USBHTAG,"USB connection event");
    resp.status = USB_STAT_ATTACHED;

    if (xQueueSend(usb_cplt_queue, &resp, 0)!=pdTRUE) {
        ESP_LOGE(USBHTAG, "Cannot queue!!!");
    }
}

static void usbh__ll_task(void *pvParam)
{
    struct usbcmd cmd;
    TickType_t tick;
    while (1) {
#ifdef __linux__
        tick = 1;
#else
        tick = portMAX_DELAY;
#endif
        if (xQueueReceive(usb_cmd_queue, &cmd, tick)==pdTRUE)
        {
            if (cmd.cmd==USB_CMD_INTERRUPT) {
                usb_ll__interrupt();
            } else {
                // Handle command, pass to low-level if needed.
                if (cmd.cmd==USB_CMD_SUBMITREQUEST) {
                    struct usb_request *req = (struct usb_request*)cmd.data;
                    usbh__submit_request_to_ll(req);
                }
            }
        }
#ifdef __linux__
        usb_ll__idle();
#endif
    }
}


#if 0
void IRAM_ATTR usb__isr_handler(void* arg)
{
    struct usbcmd cmd;
    cmd.cmd = USB_CMD_INTERRUPT;
    xQueueSendFromISR(usb_cmd_queue, &cmd, NULL);
}
#endif

void usb__trigger_interrupt(void)
{
    struct usbcmd cmd;
    cmd.cmd = USB_CMD_INTERRUPT;
    xQueueSend(usb_cmd_queue, &cmd, portMAX_DELAY);
}

static void usbh__reset(void)
{
    usb_ll__reset();

    vTaskDelay(100 / portTICK_RATE_MS);
}

char *usbh__string_unicode8_to_char(const uint8_t  *src, unsigned len)
{
    if (len&1) {
        ESP_LOGE(TAG,"String len is odd!");
        return NULL;
    }
    len>>=1;
    char *ret = (char*)malloc(len+1);

    if (ret==NULL)
        return ret;

    char *tptr = ret;

    while (len--) {
        uint8_t a = *src++;
        uint8_t b = *src++;
        if (a==0) {
            *tptr++=b;
        } else {
            *tptr++='?';
        }
    }
    *tptr='\0';
    return ret;
}



int usbh__claim_interface(struct usb_device *dev, struct usb_interface *intf)
{
    if (intf->claimed>0) {
        return -1; // already claimed.
    }
    intf->claimed++;
    dev->claimed++;

    return 0;
}

static void usbh__main_task(void *pvParam)
{
    struct usbresponse resp;

    while (1) {
        USBHDEBUG("Wait for completion responses");
        if (xQueueReceive(usb_cplt_queue, &resp, portMAX_DELAY)==pdTRUE)
        {
            USBHDEBUG(" >>> got status %d", resp.status);
            if (resp.status == USB_STAT_ATTACHED) {
                // Attached.
                usbhub__port_reset(&root_hub);
            }
        } else {
            ESP_LOGE(USBHTAG, "Queue error!!!");
        }
    }
}

static void usbh__request_completed(struct usb_request *req);
static int usbh__send_ack(struct usb_request *req);


static void usbh__request_failed(struct usb_request *req)
{
    // TODO: resubmit request

    switch (req->control_state) {
    case CONTROL_STATE_SETUP:
        if (req->retries--) {
            req->size_transferred = 0;
            usbh__submit_request_to_ll(req);
            return;
        }
        break;
    case CONTROL_STATE_DATA:
        // Re-issue IN
        if (req->retries--) {
            usbh__issue_setup_data_request(req);
            //usbh__submit_request_to_ll(req);
            return;
        }
        break;
    case CONTROL_STATE_STATUS:
        if (req->retries--) {
            if (req->direction==REQ_DEVICE_TO_HOST) {
                usbh__send_ack(req);
                return;
            }
        }
        break;
    }
    struct usbresponse resp;
    resp.status = USB_REQUEST_COMPLETED_FAIL;
    resp.data = req;
    xQueueSend(usb_cplt_queue, &resp, portMAX_DELAY);
    taskYIELD();
}

static inline int usbh__request_data_remain(const struct usb_request*req, unsigned last_transaction)
{
    // If we got a short packet, then we are done.

    // TODO: ensure this is smaller than EP size
    int remain = req->length - req->size_transferred;

    if (remain > req->epsize) {
        remain = req->epsize;
    }
    USBHDEBUG("Chan %d: remain %d length req %d transferred %d", req->channel,
              remain,
              req->length,
              req->size_transferred);

    if (remain>0 && (last_transaction < req->epsize)) {
        USBHDEBUG("Chan %d: Short response (%d epsize %d), returning 0", req->channel,
                  last_transaction, req->epsize);
        return 0;
    }

    return remain;
}

static int usbh__send_ack(struct usb_request *req)
{
    // Send ack
    return usb_ll__submit_request(req->channel,
                                  req->epmemaddr,
                                  PID_OUT,
                                  req->control ? '1': req->seq,      // TODO: Check seq.
                                  NULL,
                                  0,
                                  usbh__request_completed_reply,
                                  req);
}

static int usbh__wait_ack(struct usb_request *req)
{
    return usb_ll__submit_request(req->channel,
                                  req->epmemaddr,
                                  PID_IN,
                                  req->seq,      // TODO: Check seq.
                                  NULL,
                                  0,
                                  usbh__request_completed_reply,
                                  req);

}

static int usbh__issue_setup_data_request(struct usb_request *req)
{
    int remain = usbh__request_data_remain(req, req->epsize);

    if (req->direction==REQ_DEVICE_TO_HOST) {
        // send IN request.
        return usb_ll__submit_request(req->channel,
                                      req->epmemaddr,
                                      PID_IN,
                                      req->seq,
                                      req->rptr,
                                      remain,
                                      usbh__request_completed_reply,
                                      req);
    } else {
        // send OUT request.
        return usb_ll__submit_request(req->channel,
                                      req->epmemaddr,
                                      PID_OUT,
                                      req->seq,
                                      req->rptr,
                                      remain,
                                      usbh__request_completed_reply,
                                      req);
    }
}

/*
 Example transfers:

 H2D, no data phase:
                             SM state
 * -> SETUP                  send, enter CONTROL_STATE_SETUP
 * <- ACK                    Interrupt, send IN request, enter CONTROL_STATE_STATUS
 * -> IN
 * <- DATAX(0len)
 * -> ACK                    Interrupt, completed

 H2D, data phase:

 * -> SETUP                  send, enter CONTROL_STATE_SETUP
 * <- ACK                    Interrupt, send OUT request, enter CONTROL_STATE_DATA
 * -> OUT  ]
 * <- ACK  ] times X         Interrupt, if more send OUT, stay, , else send IN, enter CONTROL_STATE_STATUS
 * -> IN
 * <- DATAX(0len)
 * <- ACK                    Interrupt, completed

 D2H, data phase:

 * -> SETUP                  send, enter CONTROL_STATE_SETUP
 * <- ACK                    Interrupt, send IN request, enter CONTROL_STATE_DATA
 * -> IN  ]
 * <- ACK  ] times X         Interrupt, if more send IN, stay , else send OUT(0), enter CONTROL_STATE_STATUS
 * -> OUT (0len)
 * <- ACK                    Interrupt, completed.

 */


static int usbh__control_request_completed_reply(uint8_t chan, uint8_t stat, struct usb_request *req)
{
    uint8_t rxlen = 0;

    if (stat & 1) {
        // For libusb, all completes immediatly


        switch (req->control_state) {
        case CONTROL_STATE_SETUP:

            /* Setup completed. If we have a data phase, send data */
            req->size_transferred = 0;
            req->seq = 1;

            if (req->direction==REQ_DEVICE_TO_HOST) {
                usb_ll__read_in_block(chan, req->rptr, &rxlen);
            }
            req->rptr += rxlen;
            req->size_transferred += rxlen;

            usbh__request_completed(req);
        }



    } else {
        ESP_LOGE(USBHTAG, "Request failed 0x%02x",stat);
        usbh__request_failed(req);
        return -1;
    }
    return 0;

}
int usbh__request_completed_reply(uint8_t chan, uint8_t stat, void *data)
{
    struct usb_request *req = (struct usb_request*)data;

    if (req->control) {
        return usbh__control_request_completed_reply(chan,stat,req);
    }

    return 0;
}

void usbh__submit_request(struct usb_request *req)
{
    struct usbcmd cmd;
    cmd.cmd = USB_CMD_SUBMITREQUEST;
    cmd.data = req;
    xQueueSend( usb_cmd_queue, &cmd, portMAX_DELAY);
    taskYIELD();
}


static void usbh__submit_request_to_ll(struct usb_request *req)
{
    if (req->control) {
        USBHDEBUG("Submitting %p", req);
        usb_ll__submit_request(req->channel,
                               req->epmemaddr,
                               PID_SETUP,
                               0, // Always use DATA0
                               req->setup.data,
                               sizeof(req->setup),
                               usbh__request_completed_reply,
                               req);
    } else {
        if (req->direction==REQ_DEVICE_TO_HOST) {
            usb_ll__submit_request(req->channel,
                                   req->epmemaddr,
                                   PID_IN,
                                   req->seq,
                                   req->target,
                                   req->length > 64 ? 64: req->length,
                                   usbh__request_completed_reply,
                                   req);
        } else {
            if (req->length) {
                usb_ll__submit_request(req->channel,
                                       req->epmemaddr,
                                       PID_OUT,
                                       req->seq,
                                       req->target,
                                       req->length > 64 ? 64: req->length,
                                       usbh__request_completed_reply,
                                       req);
            }
        }

    }
}

int usbh__get_descriptor(struct usb_device *dev,
                         uint8_t  req_type,
                         uint16_t value,
                         uint8_t* target,
                         uint16_t length)
{
    struct usb_request req = {0};

    req.device = dev;
    req.target = target;
    req.length = length;
    req.rptr = target;
    req.size_transferred = 0;
    req.direction = REQ_DEVICE_TO_HOST;
    req.control = 1;
    req.retries = 3;
    req.channel = dev->ep0_chan;
    req.epsize = dev->ep0_size;


    req.setup.bmRequestType = req_type | USB_DEVICE_TO_HOST;
    req.setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.setup.wValue = __le16(value);

    if ((value & 0xff00) == USB_DESC_STRING)
    {
        // Language
        req.setup.wIndex = __le16(0x0409);
    }
    else
    {
        req.setup.wIndex = __le16(0);
    }
    req.setup.wLength = length;
    req.channel =  dev->ep0_chan;
    usbh__submit_request(&req);

    // Wait.
    if (usbh__wait_completion(&req)<0) {
        return -1;
    }

    return 0;
}


void usb_ll__overcurrent_callback()
{
    ESP_LOGE(USBHTAG, "Overcurrent");
    systemevent__send(SYSTEMEVENT_TYPE_USB, SYSTEMEVENT_USB_OVERCURRENT);
}

static void usbh__request_completed(struct usb_request *req)
{
    struct usbresponse resp;
    resp.status = USB_REQUEST_COMPLETED_OK;
    resp.data = req;
    xQueueSend(usb_cplt_queue, &resp, portMAX_DELAY);
    taskYIELD();
}

int usbh__set_configuration(struct usb_device *dev, uint8_t configidx)
{
    struct usb_request req = {0};

    req.device = dev;
    req.target = NULL;
    req.length = 0;
    req.rptr = NULL;
    req.size_transferred = 0;
    req.direction = REQ_HOST_TO_DEVICE;
    req.control = 1;

    req.setup.bmRequestType = USB_REQ_RECIPIENT_DEVICE | USB_REQ_TYPE_STANDARD | USB_HOST_TO_DEVICE;
    req.setup.bRequest = USB_REQ_SET_CONFIGURATION;
    req.setup.wValue = __le16(configidx);
    req.setup.wIndex = __le16(0);
    req.setup.wLength = __le16(0);

    req.channel =  dev->ep0_chan;
    USBHDEBUG("Submitting address request");
    usbh__submit_request(&req);

    // Wait.
    if (usbh__wait_completion(&req)<0) {
        ESP_LOGE(USBHTAG,"Device not accepting address!");
        return -1;
    }
    USBHDEBUG("Submitted config request");

    return 0;

}

void usbh__dump_info()
{
    usb_ll__dump_info();
}

uint32_t usbh__get_device_id(const struct usb_device*dev)
{
    uint32_t id = dev->device_descriptor.idVendor;
    id<<=16;
    id += dev->device_descriptor.idProduct;
    return id;
}

int usbh__init()
{
    usb_cmd_queue  = xQueueCreate(4, sizeof(struct usbcmd));
    usb_cplt_queue = xQueueCreate(4, sizeof(struct usbresponse));

    xTaskCreate(usbh__ll_task, "usbh_ll_task", 4096, NULL, 10, NULL);
    xTaskCreate(usbh__main_task, "usbh_main_task", 4096, NULL, 11, NULL);

    root_hub.reset = &usbh__reset;

    usb_ll__init();

    return 0;
}

int usbh__wait_completion(struct usb_request *req)
{
    struct usbresponse resp;
    if (xQueueReceive(usb_cplt_queue, &resp, portMAX_DELAY)==pdTRUE)
    {
        USBHDEBUG("USB completed status %d", resp.status);
        if (resp.status == USB_REQUEST_COMPLETED_OK) {
            if (resp.data == req) {
                return 0;
            } else {
                ESP_LOGE(USBHTAG, "NOT FOR US!");
                return -1;
            }
        } else {
            // Error response
            if (resp.data == req) {
                ESP_LOGE(USBHTAG, "Status error!");
               return -1;
            } else {
                ESP_LOGE(USBHTAG, "NOT FOR US!");
                return -1;
            }
       }
    }
    return -1;
}