#!/usr/bin/env python3

import enum
import configparser
import os
import logging
import logging.handlers
import signal

import pyudev as udev

import pyric
import pyric.pyw as pyw
import pyric.utils.hardware as pywhw

class Card(enum.Enum):
    ath9k = 1
    rtl88xx = 2

config_filename = "/etc/default/wifi_config"

udev_context = udev.Context()

def handler(signum, frame):
    """Re-read the configuration file on a SIGHUP signal"""
    if signum == signal.SIGHUP:
        net.reload_config(config_filename)

class Network(object):
    """Create and control monitor mode wifi interfaces"""

    def __init__(self):
        self.m_devices = []

    def read_config(self, filename):
        """ Read the configuration file """
        self.config_filename = filename
        self.m_config = configparser.ConfigParser()
        self.m_config['DEFAULT'] = {
            'frequency': '2412',
            'txpower': '40',
            'bitrate': '11',
            'loglevel': 'error'
        }
        try:
            self.m_config.read(filename)
        except:
            logging.error("Error reading the configuration file: ", filename)
            exit

        # Configure the logger
        log_level = getattr(logging, self.m_config['DEFAULT'].get('loglevel').upper())
        if not isinstance(log_level, int):
            print("Invalid log level: %s - setting to info" % (args.loglevel))
            log_level = logging.INFO
        logger = logging.getLogger('wifi_config')
        logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s: %(message)s", datefmt="%H:%M:%S", \
                            handlers = [logging.handlers.SysLogHandler(address = "/dev/log")])

        # Configure the reload signal handler
        signal.signal(signal.SIGHUP, handler)

    def reload_config(self, filename):
        logging.debug("Reload config called")

        # Re-read the configuration file
        self.read_config(self.config_filename)

        # Re-configure all the interfaces
        for iface in net.monitor_interfaces():
            net.configure_interface(iface)

    def supports_monitor(self, device_name):
        """ Check if this card support monitor mode """
        try:
            card = pyw.getcard(device_name)
            if 'monitor' in pyw.devmodes(card):
                return device_name
            return True

        except pyric.error as e:
            return False

    def monitor_interfaces(self):
        """Get a list of interfaces that support monitor mode"""

        # Get a list of all the interfaces.
        ret = []
        for iface in pyw.interfaces():
            if self.supports_monitor(iface):
                # Add the card to the list
                ret.append(iface)

        # Return the collected list
        return ret

    def configure_interface(self, interface):
        """Configure the given card in monitor mode"""

        # See if this device has been unplugged
        if interface in self.m_devices:
            try:
                card = pyw.getcard(device_name)
            except:
                self.m_devices.remove(interface)
                return None
            
        # Determine the type of card for this interface
        try:
            driver = pywhw.ifcard(interface)[0]
            logging.debug("Found wifi card with driver: %s", (driver))
            if driver == 'rtl88xxau':
                type = Card.rtl88xx
            elif driver == 'ath9k_htc':
                type = Card.ath9k
            else:
                logging.debug("Unknown type type: %s", driver)
                return None

        except Exception as e:
            print(e)
            return None

        # Retrieve the configuration file
        logging.debug("Configuring: %s of type %s" % (interface, type.name))
        frequency = self.m_config[type.name].getint('frequency', 0)
        if frequency == 0:
            frequency = self.m_config['DEFAULT'].getint('frequency', 0)
        txpower = self.m_config[type.name].getint('txpower', 0)
        if txpower == 0:
            txpower = self.m_config['DEFAULT'].getint('txpower', 0)
        bitrate = self.m_config[type.name].getint('bitrate', 0)
        if bitrate == 0:
            bitrate = self.m_config['DEFAULT'].getint('bitrate', 0)

        # Get the card for this interface
        try:
            card = pyw.getcard(interface)
        except pyric.error as e:
            logging.error("Error connecting to the interface: " + interface)
            return None

        # Configure the bitrate for this card
        # This is not supported by pyric, so we have to do it manually.
        # if bitrate != 0 and type == Card.ath9k:
        #     try:
        #         pyw.down(card)
        #         pyw.modeset(card, 'monitor', flags=['other bss'])
        #         pyw.up(card)
        #         logging.debug("Setting the bitrate on interface " + interface + " to " + str(bitrate))
        #         if os.system("iw dev " + card.dev + " set bitrates legacy-2.4 " + str(bitrate)) != 0:
        #             #if os.system("iwconfig " + card.dev + " rate 54M fixed") != 0:
        #             logging.error("Error setting the bitrate for: " + interface)
        #             return None
        #         pyw.down(card)
        #     except pyric.error as e:
        #         logging.error("Error setting the bitrate for: " + interface)
        #         logging.error(e)
        #         return None

        # Bring up the card in monitor mode
        try:
            pyw.modeset(card, 'monitor')
        except pyric.error as e:
            logging.error(interface + " does not support monitor mode")
            return None

        # Try to configure the transmit power level (some cards don't support this)
        try:
            pyw.txset(card, txpower, 'fixed')
        except pyric.error as e:
            os.system("iw dev " + card.dev + " set txpower fixed " + str(txpower))

        # Bring the interface up
        try:
            pyw.up(card)
        except pyric.error as e:
            logging.error("Error bringing up the interface: " + card.dev)
            logging.error(e)
            return False

        # Configure the frequency
        try:
            logging.debug("Setting the frequency on interface " + interface + " to " + str(frequency))
            pyw.freqset(card, frequency, None)
            error = False

        except pyric.error as e:
            error = True

        # Try to configure the frequency using iwconfig
        if error:
            try:
                logging.debug("Setting the frequency on interface " + interface + " to " + str(frequency) + " using iwconfig")
                os.system("iwconfig %s freq %sM" % (card.dev, str(frequency)))
            except Exception as e:
                logging.error("Error setting the wifi frequency on: " + card.dev)
                logging.error(e)
                return False

        # Add this to the list of interfaces
        logging.debug("Configured: %s" % (interface))
        self.m_devices.append(interface)

        return card

net = Network()
net.read_config(config_filename)

# Configure any existing devices
for iface in net.monitor_interfaces():
    net.configure_interface(iface)

# Monitor for any network adapters getiting plugged in or unplugged
monitor = udev.Monitor.from_netlink(udev_context)
monitor.filter_by('net')
for device in iter(monitor.poll, None):
    net.configure_interface(device.sys_name)
