
import re
import os
import logging
import subprocess
from configparser import ConfigParser

class rtl88xxau(object):
    """Configure the rtl8812au wifi adapter"""
    wfb_config_filename = "/etc/default/wfb_bridge"

    def __init__(self, interface):
        self.interface = interface

    def name(self):
        return "rtl88xx"

    def configure(self, interface, frequency, txpower, bitrate, mcs, stbc, ldpc):

        # Configure the wfb bridge parameters based on the wifi card.
        parser = ConfigParser()
        parser.read(self.wfb_config_filename)
        parser.set('global', 'mcs', '0' if not mcs else '1')
        parser.set('global', 'stbc', '0' if not stbc else '1')
        parser.set('global', 'ldpc', '0' if not ldpc else '1')
        with open(self.wfb_config_filename, 'w') as configfile:
            parser.write(configfile)

        # Bring this card is down
        try:
            os.system("ip link set " + interface + " down")
        except Exception as e:
            logging.error("Error bringing the interface down: " + interface)
            logging.error(e)
            return False

        # Configure the card in monitor mode
        try:
            os.system("iw dev " + interface + " set type monitor")
        except:
            logging.error(interface + " does not support monitor mode")
            return None

        # Configure the transmit power level
        try:
            # First update the power override parameter
            with open("/sys/module/88XXau/parameters/rtw_tx_pwr_idx_override", "w") as fp:
                fp.write(str(txpower))
            # Now "kick" the driver to ensure the new value is read.
            os.system("iw dev " + interface + " set txpower fixed 3000")
        except Exception as e:
            logging.warning("Could not set txpower on " + interface)
            logging.warning(e)

        # Bring the interface up
        try:
            os.system("ip link set " + interface + " up")
        except Exception as e:
            logging.error("Error bringing the interface up: " + interface)
            logging.error(e)
            return False

        # Configure the frequency
        try:
            logging.debug("Setting the frequency on interface " + interface + " to " +
                          str(frequency) + " using iw")
            os.system("iw dev %s set freq %s" % (interface, str(frequency)))
        except Exception as e:
            logging.error("Error setting the wifi frequency on: " + interface)
            logging.error(e)
            return False

        return True
