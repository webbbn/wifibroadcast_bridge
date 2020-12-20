[![Build Status](https://travis-ci.com/webbbn/wifibroadcast_bridge.svg?branch=master)](https://travis-ci.com/webbbn/wifibroadcast_bridge)   [![Actions Status](https://github.com/OpenHD/wifibroadcast_bridge/workflows/build-debs/badge.svg) [ ![Download](https://api.bintray.com/packages/webbbn/openhd_test/wifibroadcast_bridge/images/download.svg) ](https://bintray.com/webbbn/openhd_test/wifibroadcast_bridge/_latestVersion)

# wifibroadcast_bridge
A UDP bridge over a wifibroadcast link

Based on code and ideas from the [OpenHD](https://github.com/HD-Fpv/Open.HD "OpenHD"), [EZ-WifiBroadcast](https://github.com/rodizio1/EZ-WifiBroadcast "EZ-WifiBroadcast"), and [svpcom wifibroadcast](https://github.com/svpcom/wifibroadcast "svpcom wifibroadcast")

## Installing from source

### Installing dependencies

~~~
sudo apt-get install -y libpcap-dev python3-pyudev libpcap0.8-dev python3-pip python3-setuptools python3-wheel python3-pyudev cmake firmware-ath9k-htc liblog4cpp5-dev libnl-3-dev libnl-genl-3-dev libudev-dev
~~~

### Update the submodules

~~~
git submodule update --init
~~~

### Compiling the software

~~~
mkdir build
cd build
cmake ..
make
~~~

### Install to a local directory

~~~
cmake -DCMAKE_INSTALL_PREFIX=<install directory> ..
make install
~~~

### Install into system directories

~~~
cmake -DCMAKE_INSTALL_PREFIX=/ ..
sudo make install
~~~

### Activate system services (after intalling in root)

~~~
sudo ../scripts/postinst
~~~

## Create an installable .deb file

~~~
cpack
~~~

## Configuration

Wifibroadcast_bridge consists of a single program (wfb_bridge) that configures the wifi interface(s), and sends and receives to/from multiple UDP ports. There is a single configuration file to configure the interfaces and ports, which is normally stored in /etc/wfb_bridge.conf. The configuration parameters are documented in the configuration file.

## Starting and enabling the services

This package adds a single systemd services that can be started, stopped, and enabled at boot time.

#### Starting the services

~~~
sudo systemctl start wfb_bridge
~~~

#### Stopping the services

~~~
sudo systemctl stop wfb_bridge
~~~

#### Enabling startup at boot

~~~
sudo systemctl enable wfb_bridge
~~~

#### Restarting the services

~~~
sudo systemctl restart wfb_bridge
~~~

## Device support

### rtl8812au

It is highly recommended to use rtl8812au based wifi adapters with version v5.6.4.2 of the aircract-ng wifi driver (https://github.com/aircrack-ng/rtl8812au). This has been well tested and works very well at bit rates at least up to 18Mbps or so.

The following patch is required to dynamically change the transmit power:

``` diff
--- a/hal/rtl8812a/rtl8812a_phycfg.c
+++ b/hal/rtl8812a/rtl8812a_phycfg.c
@@ -39,7 +39,7 @@ int transmit_power_boost = 2;
 // Transmit Power Override
 // This value completely overrides the driver's calculations and uses only one value for all transmissions.
 // Zero disables it. Default: 0
-int transmit_power_override = 0;
+extern int rtw_tx_pwr_idx_override;
 
 /* Manual Transmit Power Control */
 
@@ -592,8 +592,8 @@ PHY_GetTxPowerIndex_8812A(
 	by_rate_diff = by_rate_diff > limit ? limit : by_rate_diff;
 	power_idx = base_idx + by_rate_diff + tpt_offset + extra_bias + transmit_power_boost;
 
-	if (transmit_power_override != 0)
-		power_idx = transmit_power_override;
+	if (rtw_tx_pwr_idx_override)
+		power_idx = rtw_tx_pwr_idx_override;
 	if (power_idx < 1)
 		power_idx = 1;
 
diff --git a/os_dep/linux/os_intfs.c b/os_dep/linux/os_intfs.c
index 1085d6c..d6a3075 100644
--- a/os_dep/linux/os_intfs.c
+++ b/os_dep/linux/os_intfs.c
@@ -23,6 +23,8 @@ MODULE_AUTHOR("Realtek Semiconductor Corp.");
 MODULE_VERSION(DRIVERVERSION);
 
 /* module param defaults */
+int rtw_tx_pwr_idx_override = 45;
+module_param(rtw_tx_pwr_idx_override, int, 0644);
 int rtw_chip_version = 0x00;
 int rtw_rfintfs = HWPI;
 int rtw_lbkmode = 0;/* RTL8712_AIR_TRX; */
```

It is critical to add the following options to /etc/modprobe.d/rtl8812au.conf:

options 88XXau rtw_monitor_disable_1m=1

This option allows the monitor mode interface to transit at full data rate. Without this option, monitor mode data transfers are limited to 1Mbps!

### ar9271 / ar7010

The ar9271 (and other devices supported by the ath9k kernel driver) have also been used, but they do not support bit rates above around 8-10 Mbps, require multiple kernel patches, and generally do not perform nearly as well as the rtl8812au.
