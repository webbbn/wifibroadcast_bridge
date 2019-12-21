
import re
import os
import logging
import subprocess

class rtl88xxau(object):
    """Configure the rtl8812au wifi adapter"""
    conf_file = "/lib/modprobe.d/rtl8812au.conf"

    def __init__(self, interface):
        self.interface = interface

    def name(self):
        return "rtl88xx"

    def config_power(self, txpower):
        """Set the power level for this card"""

        # Is it already set?
        p = subprocess.Popen(['grep', 'rtw_tx_pwr_idx_override=' + str(txpower), self.conf_file], stdout=subprocess.PIPE)
        out, err = p.communicate()
        if out.decode('utf-8') != '':
            logging.debug("Not changing power level configuration")
            return True

        # Unload the module
        try:
            os.system("rmmod 88XXau")
        except:
            pass

        # Change the power in the configuration file
        try:
            with open(self.conf_file, "r+") as fp:
                line = fp.read().splitlines()[0]
                # Don't just replace, since the string might not be in the line
                line = re.sub(r' rtw_tx_pwr_idx_override=[^ ]+', '', line)
                # Clear the file
                fp.truncate(0)
                fp.seek(0)
                # Write out the updated line
                fp.write("%s rtw_tx_pwr_idx_override=%s \n" % (line.strip(), txpower))
        except Exception as e:
            logging.error("Error setting txpower on: " + self.interface)
            logging.error(e)
            return False

        # Reload the module
        try:
            os.system("modprobe 88XXau")
        except:
            pass

        logging.info("Configured %s card with power: %d" % (self.name(), txpower))
        return True
        
    def configure(self, interface, frequency, txpower, bitrate):

        # Configure the transmit power level (some cards don't support this)
        if not self.config_power(txpower):
            return False

        # Try to bring up the interface
        # The rtl8812au v5.6.4.1 driver appears to need to be brought up before configuration
        # Unfortunately I haven't been able to get that version working yet...
        try:
            os.system("ifconfig " + interface + " up")
        except Exception as e:
            logging.error("Error bringing the interface up: " + interface)
            logging.error(e)
            return False

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
