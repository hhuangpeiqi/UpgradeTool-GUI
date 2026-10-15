#include "qdl_bridge.h"
#include <libusb.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void read_string(libusb_device_handle *handle, int index, char *out, size_t size)
{
    if (!handle || !index) return;
    int n = libusb_get_string_descriptor_ascii(handle, index, (unsigned char *)out, size - 1);
    if (n > 0) out[n] = 0;
}

void qtq_read_usb_serial(libusb_device_handle *handle, int serial_index, int product_index,
                          unsigned int vid, char *out, size_t size)
{
    out[0] = 0;
    read_string(handle, serial_index, out, size);
    if (!out[0] && vid == 0x05c6) {
        char product[256] = {0}; read_string(handle, product_index, product, sizeof(product));
        char *serial = strstr(product, "_SN:");
        if (serial) { serial += 4; serial[strcspn(serial, " _")] = 0;
            strncpy(out, serial, size - 1); out[size - 1] = 0; }
    }
}

int qtq_discover_usb(struct qtq_usb_endpoint **endpoints, size_t *count)
{
    libusb_context *context = NULL;
    libusb_device **devices = NULL;
    struct qtq_usb_endpoint *rows = NULL;
    size_t used = 0, capacity = 0;
    int error = 0;
    *endpoints = NULL; *count = 0;
    if (libusb_init(&context)) return -EIO;
    ssize_t n = libusb_get_device_list(context, &devices);
    if (n < 0) { error = -EIO; goto done; }
    for (ssize_t i = 0; i < n; ++i) {
        struct libusb_device_descriptor descriptor;
        struct libusb_config_descriptor *config = NULL;
        libusb_device_handle *handle = NULL;
        if (libusb_get_device_descriptor(devices[i], &descriptor)) continue;
        if (libusb_get_active_config_descriptor(devices[i], &config)) continue;
        int relevant = 0;
        for (int j = 0; j < config->bNumInterfaces; ++j)
            for (int k = 0; k < config->interface[j].num_altsetting; ++k)
                relevant |= config->interface[j].altsetting[k].bInterfaceClass == 0xff;
        if (!relevant) { libusb_free_config_descriptor(config); continue; }
        struct qtq_device base = {0};
        base.vid = descriptor.idVendor; base.pid = descriptor.idProduct;
        base.bus = libusb_get_bus_number(devices[i]);
        base.address = libusb_get_device_address(devices[i]);
        int ports = libusb_get_port_numbers(devices[i], base.ports, sizeof(base.ports));
        base.port_count = ports > 0 ? (unsigned int)ports : 0;
        /* Opening a handle is only for descriptor reads. Driver ownership and
         * protocol data are untouched; failure does not hide the device. */
        base.access_error = libusb_open(devices[i], &handle);
        qtq_read_usb_serial(handle, descriptor.iSerialNumber, descriptor.iProduct,
            base.vid, base.serial, sizeof(base.serial));
        for (int j = 0; j < config->bNumInterfaces; ++j) {
            for (int k = 0; k < config->interface[j].num_altsetting; ++k) {
                const struct libusb_interface_descriptor *alt = &config->interface[j].altsetting[k];
                if (alt->bInterfaceClass != 0xff) continue;
                struct qtq_usb_endpoint row = {0}; row.device = base;
                row.device.interface_number = alt->bInterfaceNumber;
                row.device.alternate = alt->bAlternateSetting;
                row.cls = alt->bInterfaceClass; row.subclass = alt->bInterfaceSubClass;
                row.protocol = alt->bInterfaceProtocol;
                for (int ep = 0; ep < alt->bNumEndpoints; ++ep) {
                    const struct libusb_endpoint_descriptor *endpoint = &alt->endpoint[ep];
                    if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                    if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        row.device.in = endpoint->bEndpointAddress; row.in_size = endpoint->wMaxPacketSize;
                    } else {
                        row.device.out = endpoint->bEndpointAddress; row.out_size = endpoint->wMaxPacketSize;
                    }
                }
                read_string(handle, alt->iInterface, row.name, sizeof(row.name));
                row.device.mode = qtq_classify(base.vid, base.pid,
                    qtq_download_interface(row.cls, row.subclass, row.protocol,
                        row.device.in, row.device.out, row.in_size, row.out_size),
                    row.subclass == 0x42 && row.protocol == 1,
                    qtq_known_diag(base.vid, base.pid, row.device.interface_number) || qtq_diag_name(row.name));
                if (used == capacity) {
                    size_t next = capacity ? capacity * 2 : 16;
                    void *memory = realloc(rows, next * sizeof(*rows));
                    if (!memory) { error = -ENOMEM; goto device_done; }
                    rows = memory; capacity = next;
                }
                rows[used++] = row;
            }
        }
device_done:
        if (handle) libusb_close(handle);
        libusb_free_config_descriptor(config);
        if (error) goto done;
    }
done:
    if (devices) libusb_free_device_list(devices, 1);
    libusb_exit(context);
    if (error) { free(rows); return error; }
    *endpoints = rows; *count = used; return 0;
}
