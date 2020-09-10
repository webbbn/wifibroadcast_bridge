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

It is critical to add the following option to /etc/modprobe.d/rtl8812au.conf:

options 88XXau rtw_monitor_disable_1m=1

Without this option, monitor mode data transfers are limited to 1Mbps.

### ar9271 / ar7010

The ar9271 (and other devices supported by the ath9k kernel driver) have also been used, but they do not support bit rates above around 8-10 Mbps, require multiple kernel patches, and generally do not perform nearly as well as the rtl8812au.
