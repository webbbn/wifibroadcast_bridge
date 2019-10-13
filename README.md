[![Build Status](https://travis-ci.com/webbbn/wifibroadcast_bridge.svg?branch=master)](https://travis-ci.com/webbbn/wifibroadcast_bridge)

# wifibroadcast_bridge
A UDP bridge over a wifibroadcast link

Based on code and ideas from the [OpenHD](https://github.com/HD-Fpv/Open.HD "OpenHD"), [EZ-WifiBroadcast](https://github.com/rodizio1/EZ-WifiBroadcast "EZ-WifiBroadcast"), and [svpcom wifibroadcast](https://github.com/svpcom/wifibroadcast "svpcom wifibroadcast")

## Installing dependencies

~~~
sudo apt-get install -y libboost-all-dev libpcap-dev python3-pyudev libpcap0.8-dev
~~~

## Compiling the software

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

### Create an installable .deb file

~~~
cpack
~~~
