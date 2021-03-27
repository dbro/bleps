bleps : Bluetooth LE packet sniffer
===================================
_displays raw Bluetooth LE packets captured from devices with TI CC2540 chipsets_

Dan Brown, March 2021
https://github.com/dbro/bleps

bleps monitors Bluetooth LE traffic and prints raw packet data on the command line. The packet data can then be piped or saved, to enable other programs to parse and analyze it.

My own purpose for this packet sniffer is to listen for broadcasted packets coming from a [Xiaomi bathroom scale](https://www.mi.com/global/mi-smart-scale-2/).

Devices supported
-----------------

Tested with a generic USB adapter containing a Texas Instruments [CC2540](https://www.ti.com/product/CC2540) chipset [available from Aliexpress](https://www.aliexpress.com/item/1005001863186352.html).

![CC2540 USB adapter](https://github.com/dbro/bleps/raw/master/ble-usb-cc2540.jpg)

For consistent results, it may be a good idea to flash the firmware of the device with the hex file available from Texas Instruments' packet sniffer software (link below).

Examples of use
---------------

To capture and display all packets from a USB device:

    $ sudo bleps

Output will show the packets encoded as hexadecimal:

    001C005D12F44117D6BE898E430C16C825740760FD5FF2E20A6868724D0E25
    003200410211422DD6BE898E4622FFE94549F95803039FFE17169FFE00000000000000000000000000000000000000009ABB5A32A5
    0029006109EB4224D6BE898E4019C98AD1AE346C02011A020A070CFF4C001007231FADE53AE238C9CB921AA5
    001C002B04EE4217D6BE898EC30CFFE94549F958C98AD1AE346CC8BA6732A5
    001600F605F04211D6BE898E4406C98AD1AE346C6F4FD81AA5
    ....

To pipe packets containing a known string (eg. the MAC address of a specific device) to another program for processing:

    $ sudo bleps | grep MACADDRESS | processor

For parsing packet information, there is useful information [here](http://shukra.cedt.iisc.ernet.in/edwiki/Smart_RF_Equivalent).

Installation
------------

    $ make
    $ sudo make install

Depends on these packages: build-essentials, libusb-1.0-0-dev, pkg-config

Acknowledgements and alternatives
---------------------------------

* TI offers a Windows application called [SmartRF Packet Sniffer](https://www.ti.com/tool/PACKET-SNIFFER) which includes firmware for CC2540 devices.
* Bertrik Sikken's [cc2540](https://github.com/bertrik/cc2540) repository and [webpage](https://revspace.nl/CC2540) were very helpful to me. Thanks!

Some other github repositories (which I have not tested)
* [pyCCSniffer](https://github.com/andrewdodd/pyCCSniffer)
* [Blesniffer](https://github.com/ura14h/Blesniffer)
* [SnifferTICC](https://github.com/bergeraaron/SnifferTICC)
* [blesensor](https://github.com/jige003/blesensor)

Known issues
------------

The stream of packets sometimes pauses for a few seconds before continuing. For my use case, this is acceptable behavior.
