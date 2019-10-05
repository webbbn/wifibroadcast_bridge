# wifibroadcast_bridge
A UDP bridge over a wifibroadcast link

Based on code and ideas from the [OpenHD](https://github.com/HD-Fpv/Open.HD "OpenHD"), [EZ-WifiBroadcast](https://github.com/rodizio1/EZ-WifiBroadcast "EZ-WifiBroadcast"), and [svpcom wifibroadcast](https://github.com/svpcom/wifibroadcast "svpcom wifibroadcast")

## Installing dependencies

~~~
sudo apt-get install python3-udev libpcap-0.8
~~~

## Compiling the software

~~~
mkdir build
cd build
cmake ..
make
cpack
~~~
