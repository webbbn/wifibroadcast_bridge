
import os
import logging
import subprocess

class rtl88xxau(object):
    """Configure the rtl8812au wifi adapter"""
    conf_file = "/etc/modprobe.d/rtl88au.conf"

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
            os.system("sed -i 's/rtw_tx_pwr_idx_override=[^ ]+/rtw_tx_pwr_idx_override='%s'/g' %s" % (txpower, self.conf_file))
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
        
    def configure(self, interface, frequency, txpower):

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

        # Bring this this card is down
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
            logging.error("Error bringing the interface down: " + interface)
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
