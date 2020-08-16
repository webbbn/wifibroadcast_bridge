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

**Note:** The only parameter that should need to be different between the ground and air side is the mode, which should be ground on one side and air on the other.

Wifibroadcast_bridge includes a system service for automatically configuring supported wifi devices (wifi_config) and a system service creates a bridge between two computers over a raw wifi link (wfb_bridge).

The following describes the configuration process for each service.

### Configure wifi_config

The configuration file for wifi_config is installed in /etc/default/wifi_config. The file is standard INI format and contains configuration options for configuring the wifi devices. Supported wifi devices will be automatically configured when they are installed in the system.

#### DEFAULT

The default section defines default the parameters that will be used for device types that have not been set in the sections dedicated to specific device types.

#### ath9k

The configuration parameters for Atheros 9k based network cards (ar9271).

### rtl88xx

The configuration parameters for Realtek 88xx based network cards (rtl8812au/rtl8814au).

### Configuring wfb_bridge

Wfb_bridge includes a system service that creates a transparent, forward error corrected link over a raw wifi link.

### global

The global parameters contains parameters that correspond to all the virtual links.

The most important parameter is the mode parameter, which much be changed to "ground" for one of the two ends of the link (nominally the ground side).

### port definitions

The remainder of the sections define distinct, unidirection links that connect a UDP port on one side to a UDP output port on the other side of the link.

The direction parameter determines which side will open the receive port, and which will send packets received over the link. Up implies that the ground side will open the receive port and the air side will send. Down implies the opposite.

## Starting and enabling the services

This package adds two systemd services that can be started, stopped, and enabled at boot time.

#### Starting the services

~~~
sudo systemctl start wifi_config
sudo systemctl start wfb_bridge
~~~

#### Stopping the services

~~~
sudo systemctl stop wifi_config
sudo systemctl stop wfb_bridge
~~~

#### Enabling startup at boot

~~~
sudo systemctl enable wifi_config
sudo systemctl enable wfb_bridge
~~~

#### Restarting the services

~~~
sudo systemctl restart wifi_config
sudo systemctl restart wfb_bridge
~~~

**Note:** Once the services have been configured they should generally not have to be restarted on any hardware changes, but there are some instances where wfb_bridge will not reenable after removing and inserting a wifi adapter. This will require restarting the wfb_bridge process.
