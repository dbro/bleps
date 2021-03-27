#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <libusb.h>

#define NDEBUG // to disable debug messages
#include "dbg.h"

// bmRequestType vendor-specific direction codes
#define CTL_WRITE LIBUSB_REQUEST_TYPE_VENDOR
#define CTL_READ (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
// bRequest vendor-specific command codes for CC2540 according to https://github.com/andrewdodd/ccsniffpiper/blob/master/ccsniffpiper.py
#define GET_IDENT   0xC0
#define SET_POWER   0xC5
#define GET_POWER   0xC6
//#define ?????????   0xC9
#define SET_START   0xD0
#define SET_END     0xD1
#define SET_CHAN    0xD2

#define TIMEOUT 1000 // limit in milliseconds to wait for information from device
#define POWER_RETRIES 10

// device information from "lsusb -d 0451: -v"
#define USB_VENDOR_ID 0x0451
#define USB_PRODUCT_ID 0x16B3
#define INTERFACE_ID 0 // aka bInterfaceNumber
#define ENDPOINT_ID 0x83 // aka bEndpointAddress (0x83 = Input #3)
#define MAX_PACKET_SIZE 64

static int get_ident(libusb_device_handle *dev)
{
    uint8_t ident[32];
    char ident_hex[64];
    int ret;
    
    ret = libusb_control_transfer(dev, CTL_READ, GET_IDENT, 0x00, 0x00, ident, sizeof(ident), TIMEOUT);
    check(ret >= 0, "Failed to read device identity. Error: %s", libusb_error_name(ret));
    int i;
    for(i=0; i<ret; i++) {
        sprintf(&ident_hex[3*i], " %02X", ident[i]);
    }
    debug("Device identity%s", ident_hex);
    return 0;

error:
    return 1;
}

static int set_power(libusb_device_handle *dev, uint8_t power, int retries)
{
    check(libusb_control_transfer(dev, CTL_WRITE, SET_POWER, 0x00, power, NULL, 0, TIMEOUT) >= 0, "Failed to set device power.\n");

    // get power until it is the same as configured in set_power
    int i;
    uint8_t data;
    for (i = 0; i < retries; i++) {
        check(libusb_control_transfer(dev, CTL_READ, GET_POWER, 0x00, 0x00, &data, 1, TIMEOUT) >= 0, "Failed to read device power setting.\n");
        if (data == power) {
            return 0; // success
        }
	debug("Could not confirm power setting on attempt # %d of %d", (i+1), retries);
	usleep(100 * 1000); // 100 milliseconds or 0.1 seconds
    }
    check(1 == 0, "Failed to confirm requested device power setting after %d attempts.\n", retries);
    return 1; // failed. (same as falling through to error)

error:
    return 1;
}

static int set_channel(libusb_device_handle *dev, uint8_t channel)
{
    uint8_t data;
    data = channel & 0xFF;
    check(libusb_control_transfer(dev, CTL_WRITE, SET_CHAN, 0x00, 0x00, &data, 1, TIMEOUT) >= 0, "Failed to set channel low byte.\n");

    data = (channel >> 8) & 0xFF;
    check(libusb_control_transfer(dev, CTL_WRITE, SET_CHAN, 0x00, 0x01, &data, 1, TIMEOUT) >= 0, "Failed to set channel high byte.\n");

    return 0;

error:
    return 1;
}

static int setup(libusb_device_handle *dev, int channel)
{
    check(get_ident(dev) == 0, "Failed to get device identity.\n");
    check(set_power(dev, 0x04, POWER_RETRIES) == 0, "Failed to set power and/or confirm setting.\n"); // what does power level = 0x04 mean?
    // ? what is the 0xC9 register for ?
    check(libusb_control_transfer(dev, CTL_WRITE, 0xC9, 0x00, 0x00, NULL, 0, TIMEOUT) >= 0, "Failed to set register 0xC9.\n");
    check(set_channel(dev, channel) == 0, "Failed to set capture channel.\n");
    return 0;

error:
    return 1;
}

volatile sig_atomic_t stop_signal;
void handle_stop_signal(int signum)
{
    (void)signum;
    debug("Received stop signal %d.", signum);
    stop_signal = 1;
}

volatile sig_atomic_t is_transfer_active_signal;
void callback_read_complete(struct libusb_transfer *xfer)
{
    int i;
    is_transfer_active_signal = 0; // allow new submit to happen in loop
    //debug("Handling transfer %p with status %d", xfer, xfer->status);
    switch(xfer->status) {
        case LIBUSB_TRANSFER_COMPLETED:
            // received some data (which might be an incomplete packet)
	    if (*(bool*) xfer->user_data) { // prettyprint
                // 16 bytes per printed line
                for (i = 0; i < xfer->actual_length; i++) {
                    if ((i % 16) == 0) {
                        printf("\n%04X:", i);
                    }
                    printf(" %02X", xfer->buffer[i]);
                }
	    } else { // regular
                for (i = 0; i < xfer->actual_length; i++) {
                    printf("%02X", xfer->buffer[i]);
                }
	    }
            printf("\n");
	    break;
	// error cases below
        case LIBUSB_TRANSFER_TIMED_OUT:
	    //debug("Transfer timeout detected : %d %s", xfer->status, libusb_error_name(xfer->status));
	    break; // silent ignore timeouts
        case LIBUSB_TRANSFER_CANCELLED:
        case LIBUSB_TRANSFER_NO_DEVICE:
        case LIBUSB_TRANSFER_ERROR:
        case LIBUSB_TRANSFER_STALL:
        case LIBUSB_TRANSFER_OVERFLOW:
	    debug("Handling transfer with error status %d %s", xfer->status, libusb_error_name(xfer->status));
	    break;
    }
    return;
}

static void bulk_read_async(libusb_context *context, libusb_device_handle *dev, bool prettyprint)
{
    struct libusb_transfer *xfer = NULL;
    uint8_t data[MAX_PACKET_SIZE];
    int ret;
    struct timeval tv_zero;
    tv_zero = (struct timeval){0};
    
    // the transfer object will be reused for all submitted requests
    xfer = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(xfer, dev, ENDPOINT_ID, data, MAX_PACKET_SIZE, callback_read_complete, &prettyprint, TIMEOUT);

    is_transfer_active_signal = 0;
    signal(SIGINT, handle_stop_signal);
    signal(SIGPIPE, handle_stop_signal);
    while (!stop_signal) {
        // ready to request another packet with a transfer submit?
        if (is_transfer_active_signal == 0) {
            is_transfer_active_signal = 1; // only one transfer active at a time
            check(libusb_submit_transfer(xfer) == LIBUSB_SUCCESS, "Error submitting transfer %p : %d %s", xfer, errno, libusb_error_name(errno));
	    //debug("Submitted transfer %p", xfer);
	}
	// look if there is a transfer ready with a received packet
        check(libusb_handle_events_timeout_completed(context, &tv_zero, NULL) == LIBUSB_SUCCESS, "Event handling error detected %d %s", errno, libusb_error_name(errno));
	usleep(1000); // wait at least 0.001 seconds before checking again for packets (and possibly requesting another packet)
    }

    // clean up by attempting to cancel current transfer
    if (xfer != NULL) {
        debug("attempting to cancel transfer %p", xfer);
        ret = libusb_cancel_transfer(xfer);
        if (ret != 0) {
            debug("Failed to cancel transfer %p : %d %s", xfer, ret, libusb_error_name(ret));
        }
    }
    // request the cancelled current transfer be handled
    check(libusb_handle_events_timeout_completed(context, &tv_zero, NULL) == LIBUSB_SUCCESS, "Event handling error detected %d %s", errno, libusb_error_name(errno));

error:
    libusb_free_transfer(xfer);
    return;
}

static void sniff(libusb_context *context, uint16_t vid, uint16_t pid, int channel, bool prettyprint)
{
    libusb_device_handle *dev = libusb_open_device_with_vid_pid(context, vid, pid);
    check(dev != NULL, "Failed to open USB device %04X:%04X\n", vid, pid);
    debug("Opened USB device %04X:%04X", vid, pid);
    int ret;

    // take control device
    // first detach kernel driver if any is attached
    ret = libusb_detach_kernel_driver(dev, INTERFACE_ID);
    if (ret != LIBUSB_SUCCESS) {
        debug("Unable to detach kernel driver from device. Error %d %s\n", ret, libusb_error_name(ret));
    }
    check(libusb_claim_interface(dev, INTERFACE_ID) == LIBUSB_SUCCESS, "Failed to claim device %d.\n", INTERFACE_ID);

    check(setup(dev, channel) == 0, "Failed to set up sniffer.\n");

    // start capturing and processing packets
    check(libusb_control_transfer(dev, CTL_WRITE, SET_START, 0x00, 0x00, NULL, 0, TIMEOUT) >= 0, "Failed to start capture.\n");
    bulk_read_async(context, dev, prettyprint);

    // clean up and shut down
    check(libusb_release_interface(dev, INTERFACE_ID) == LIBUSB_SUCCESS, "Failed to release device %d , error %d %s.\n", INTERFACE_ID, errno, libusb_error_name(errno));
    libusb_close(dev);
    debug("Closed USB device %04X:%04X", vid, pid);

    return;

error:
    return;
}

int main(int argc, char *argv[])
{
    // default parameters
    int channel = 37; // channels 37,38,39 are the advertisement channels
    bool prettyprint = false;
    int opt;
    while ((opt = getopt(argc, argv, "c:p")) != -1) {
        switch (opt) {
	    case 'c':
	        channel = atoi(optarg);
		break;
	    case 'p':
		prettyprint = true;
		break;
            case '?':
		goto usage;
	    default:
		fprintf(stderr, "Unrecognized option: '-%c'\n", optopt);
		goto usage;
	}
    }

    setvbuf(stdout, NULL, _IOLBF, 0); // use line buffering
    setvbuf(stderr, NULL, _IOLBF, 0); // use line buffering
    debug("Sniffing BLE traffic on channel %d", channel);

    libusb_context *context;
    check(libusb_init(&context) == LIBUSB_SUCCESS, "Failed to initialize libusb.\n");
    check(libusb_set_option(context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING) == LIBUSB_SUCCESS, "Failed to set libusb log level.\n");
    sniff(context, USB_VENDOR_ID, USB_PRODUCT_ID, channel, prettyprint);
    libusb_exit(context);
    debug("Closed libusb.");
    
    return 0;

usage:
    fprintf(stderr, "Usage: %s [OPTIONS]\n", argv[0]);
    fprintf(stderr, "\t-c\tdefault 37\tchannel to sniff\n");
    fprintf(stderr, "\t-p\tdefault off\tpretty print packet data, with 16 separate hexadecimal bytes per line\n");
    return 1;

error:
    if (context) { libusb_exit(context); }
    return 1;
}

