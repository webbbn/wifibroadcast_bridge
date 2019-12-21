
import os
import re
import logging
import subprocess

class ath9k_htc(object):
    """Configure the ath9k_htc wifi adapter"""
    conf_file = "/etc/modprobe.d/ath9k_hw.conf"

    def __init__(self, interface):
        self.interface = interface

    def name(self):
        return "ath9k_htc"

    def config_power(self, txpower):
        """Set the power level for this card"""

        # Is it already set?
        p = subprocess.Popen(['grep', 'txpower=' + str(txpower), self.conf_file], stdout=subprocess.PIPE)
        out, err = p.communicate()
        if out.decode('utf-8') != '':
            logging.debug("Not changing power level configuration")
            return True

        # Unload the module
        try:
            os.system("rmmod ath9k_htc")
        except:
            pass

        # Change the power in the configuration file
        try:
            with open(self.conf_file, "r+") as fp:
                line = fp.read().splitlines()[0]
                # Don't just replace, since the string might not be in the line
                line = re.sub(r' txpower=[^ ]+', '', line)
                # Clear the file
                fp.truncate(0)
                fp.seek(0)
                # Write out the updated line
                fp.write("%s txpower=%s \n" % (line.strip(), txpower))
        except Exception as e:
            logging.error("Error setting txpower on: " + self.interface)
            logging.error(e)
            return False

        # Reload the module
        try:
            os.system("modprobe ath9k_htc")
        except:
            pass

        logging.info("Configured %s card with power: %d" % (self.name(), txpower))

        # Return False so that we will stop configuration at this point,
        # assuming that the card will be configured again due to the loading of the module.
        return False
        
    def configure(self, interface, frequency, txpower, bitrate):

        # Configure the transmit power level (some cards don't support this)
        if not self.config_power(txpower):
            return False

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
                logging.debug("Setting the bitrate on interface " + interface + " to " + str(bitrate))
                if os.system("iw dev " + interface + " set bitrates legacy-2.4 " + str(bitrate)) != 0:
                    logging.error("Error setting the bitrate for: " + interface)
                    return None
            except:
                logging.error("Error setting the bitrate for: " + interface)
                return None

        # Bring the card down
        try:
            os.system("ifconfig " + interface + " down")
        except Exception as e:
            logging.error("Error bringing the interface down: " + interface)
            logging.error(e)
            return False

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
            logging.debug("Setting the frequency on interface " + interface + " to " +
                          str(frequency) + " using iw")
            os.system("iw dev %s set freq %s" % (interface, str(frequency)))
        except Exception as e:
            logging.error("Error setting the wifi frequency on: " + interface)
            logging.error(e)
            return False

        return True
