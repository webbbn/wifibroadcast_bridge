
import os
import re
import logging
import subprocess
#from configparser import ConfigParser

class ar9271(object):
    """Configure the ar9271 wifi adapter"""
    wfb_config_filename = "/etc/default/wfb_bridge"

    def __init__(self, interface):
        self.interface = interface

    def name(self):
        return "ar9271"
        
    def configure(self, interface, frequency, txpower, bitrate, mcs, stbc, ldpc):

        # Configure the wfb bridge parameters based on the wifi card.
        #parser = ConfigParser()
        #parser.read(self.wfb_config_filename)
        #parser.set('global', 'mcs', '1' if mcs else '0')
        #parser.set('global', 'stbc', '1' if stbc else '0')
        #parser.set('global', 'ldpc', '1' if ldpc else '0')
        #with open(self.wfb_config_filename, 'w') as configfile:
            #parser.write(configfile)

        # Try to bring up the interface
        try:
            os.system("ifconfig " + interface + " up")
        except Exception as e:
            logging.error("Error bringing the interface up: " + interface)
            logging.error(e)
            return False

        # Configure the bitrate for this card
        if bitrate != 0:
            try:
                logging.info("Setting the bitrate on interface " + interface + " to " + str(bitrate))
                if os.system("iw dev " + interface + " set bitrates legacy-2.4 " + str(bitrate)) != 0:
                    logging.error("Error setting the bitrate for: " + interface)
            except:
                logging.error("Error setting the bitrate for: " + interface)

        # Bring the card down
        try:
            os.system("ifconfig " + interface + " down")
        except Exception as e:
            logging.error("Error bringing the interface down: " + interface)
            logging.error(e)
            return False

        # Configure the transmit power level
        try:
            logging.info("Setting txpower on interface " + interface + " to " + str(txpower))
            with open("/sys/module/ath9k_hw/parameters/txpower", "w") as fp:
                fp.write(str(txpower))
        except Exception as e:
            logging.warning("Could not set txpower on " + interface)
            logging.warning(e)

        # Configure the card in monitor mode
        try:
            os.system("iw dev " + interface + " set monitor none")
        except:
            logging.error(interface + " does not support monitor mode")
            return None

        # Try to bring up the interface
        try:
            os.system("ifconfig " + interface + " up")
        except Exception as e:
            logging.error("Error bringing the interface up: " + interface)
            logging.error(e)
            return False

        # Configure the frequency
        try:
            logging.info("Setting the frequency on interface " + interface + " to " +
                         str(frequency) + " using iw")
            os.system("iw dev %s set freq %s" % (interface, str(frequency)))
        except Exception as e:
            logging.error("Error setting the wifi frequency on: " + interface)
            logging.error(e)
            return False

        return True
