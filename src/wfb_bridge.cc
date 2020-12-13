
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <set>
#include <cstdlib>

#include <cmdparser.hpp>
#include <logging.hh>
#include <shared_queue.hh>
#include <wifibroadcast/raw_socket.hh>
#include <wifibroadcast/fec.hh>
#include <wifibroadcast/transfer_stats.hh>
#include <udp_send.hh>
#include <udp_receive.hh>
#include <wfb_bridge.hh>
#include <raw_send_thread.hh>
#include <udev_interface.hh>

double last_packet_time = 0;

std::set<std::string> parse_device_list(const INIReader &conf, const std::string &field);
bool configure_device(const std::string &device, const std::string &device_type, bool transmit,
                      bool relay, const INIReader &conf, bool &mcs, bool &stbc, bool &ldpc,
                      std::string &mode, bool do_configure);


int main(int argc, char** argv) {

  // Ensure we've running with root privileges
  auto uid = getuid();
  if (uid != 0) {
    std::cerr << "This application must be run as root or with root privileges.\n";
    return EXIT_FAILURE;
  }

  cli::Parser options(argc, argv);
  options.set_optional<bool>("n", "no_conf_wifi", false, "do not configure the wifi interfaces");
  options.set_required<std::string>
    ("c", "conf_file", "the path to the configuration file used for configuring ports");
  options.run_and_exit_if_error();

  bool do_configure = !options.get<bool>("n");
  std::string conf_file = options.get<std::string>("c");

  // Parse the configuration file.
  INIReader conf(conf_file);
  if (conf.ParseError()) {
    std::cerr << "Error reading the configuration file: " << conf_file << std::endl;
    return EXIT_FAILURE;
  }

  // Read the global parameters
  std::string log_level = conf.Get("global", "loglevel", "info");
  std::string syslog_level = conf.Get("global", "sysloglevel", "info");
  std::string syslog_host = conf.Get("global", "sysloghost", "localhost");
  std::string mode = conf.Get("global", "mode", "air");
  std::string other_mode = (mode == "air") ? "ground" : "air";
  float syslog_period = conf.GetFloat("global", "syslogperiod", 5);
  float status_period = conf.GetFloat("global", "statusperiod", 0.2);
  uint32_t timeout_us = conf.GetInteger("global", "timeout", 1000);
  std::string local_host = conf.Get("global", (mode == "air") ? "air_ip" : "ground_id", "10.1.1.1");
  std::string remote_host =
    conf.Get("global", (mode == "air") ? "ground_ip" : "air_id", "10.1.1.1");
  std::string log_to_host = conf.Get("global", "ground_id", "10.1.1.1");
  std::string netmask = conf.Get("global", "netmask", "255.255.255.0");
  uint16_t packed_status_port =
    static_cast<uint16_t>(conf.GetInteger("global", "packed_status_port", 5800));
  std::string packed_status_host = conf.Get("global", "packed_status_host", "127.0.0.1");
  uint16_t mtu = static_cast<uint16_t>(conf.GetInteger("global", "mtu", 1466));
  uint16_t max_queue_size = static_cast<uint16_t>(conf.GetInteger("global", "maxqueuesize", 200));
  uint16_t link_status_ip_port =
    static_cast<uint16_t>(conf.GetInteger("link-status", "ip_port", 5155));
  uint16_t link_status_port =
    static_cast<uint16_t>(conf.GetInteger("link-status", "port", 1));

  // Calculate the maximum data block size
  uint16_t blocksize = mtu - FEC_OVERHEAD - RAW_SOCKET_OVERHEAD;

  // Create the logger
  log4cpp::Appender *console = new log4cpp::OstreamAppender("console", &std::cout);
  console->setLayout(new log4cpp::BasicLayout());
  log4cpp::Appender *syslog = new log4cpp::SyslogAppender("syslog", argv[0]);
  syslog->setLayout(new log4cpp::BasicLayout());
  log4cpp::Category& root = log4cpp::Category::getRoot();
  console->setThreshold(get_log_level(log_level));
  root.addAppender(console);
  syslog->setThreshold(get_log_level(syslog_level));
  root.addAppender(syslog);
  LOG_INFO << "wfb_bridge running in " << mode << " mode, logging '"
           << log_level << "' to console and '" << syslog_level << "' to syslog";

  // Create the input/output message queues.
  SharedQueue<std::shared_ptr<monitor_message_t> > inqueue(max_queue_size);   // Wifi to UDP
  SharedQueue<std::shared_ptr<Message> > outqueue(max_queue_size);  // UDP to Wifi

  // Maintain a list of all the threads
  std::vector<std::shared_ptr<std::thread> > thrs;

  // Maintain statistics for out side of the connection and the other side.
  // Each side shares stats via periodic messaegs.
  TransferStats trans_stats(mode);
  TransferStats trans_stats_other(other_mode);

  // Create an interface to udev that monitors for wifi card insertions / deletions
  bool reset_wifi = false;
  UDevInterface udev(reset_wifi);
  thrs.push_back(std::make_shared<std::thread>(std::thread(&UDevInterface::monitor_thread, &udev)));

  // Create the TUN interface
  TUNInterface tun_interface;
  tun_interface.init(local_host, netmask, blocksize);

  // Create a lookup table that stores the link parameters for each IP port
  std::map<uint16_t, std::shared_ptr<Message> > port_msg_lut;
  uint16_t port_lut[RAW_SOCKET_NPORTS];

  // Create the interfaces to FEC decoders and send out the blocks received off the raw socket.
  PacketQueueP send_queue(new PacketQueue(1000));
  const std::set<std::string> &sections = conf.Sections();
  uint8_t port = 0;
  for (const auto &group : sections) {

    // Ignore sections that don't start with 'link-'
    if (group.substr(0, 5) != "link-") {
      continue;
    }

    // Get the name.
    std::string name = conf.Get(group, "name", "");

    // Get the link type
    std::string type = conf.Get(group, "type", "data");
    
    // Get the priority (optional).
    uint8_t priority = static_cast<uint8_t>(conf.GetInteger(group, "priority", 100));

    // Get the FEC parameters (optional).
    uint8_t nblocks = static_cast<uint8_t>(conf.GetInteger(group, "blocks", 0));
    uint8_t nfec_blocks = static_cast<uint8_t>(conf.GetInteger(group, "fec", 0));
    bool do_fec = ((nblocks > 0) && (nfec_blocks > 0));

    // Allocate the encoder (blocks contain a 16 bit, 2 byte size field)
    std::shared_ptr<FECEncoder> enc(new FECEncoder(nblocks, nfec_blocks, blocksize));

    // Create the FEC encoder if requested.
    WifiOptions opts;
    if (type == "data"){
      opts.link_type = DATA_LINK;
    } else if (type == "short") {
      opts.link_type = SHORT_DATA_LINK;
    } else if (type == "rts") {
      opts.link_type = RTS_DATA_LINK;
    } else {
      opts.link_type = DATA_LINK;
    }
    opts.data_rate = static_cast<uint8_t>(conf.GetInteger(group, "datarate", 18));

    // See if this link has a rate target specified.
    uint16_t rate_target = static_cast<uint16_t>(conf.GetInteger(group, "rate_target", 0));

    // Get our receive port number and transmit hosts
    uint16_t ip_port = static_cast<uint16_t>(conf.GetInteger(group, "ip_port", 0));

    // Add the prototype message for this port
    port_msg_lut[ip_port] =
      std::shared_ptr<Message>(new Message(blocksize, port, ip_port, priority, opts, enc));
    port_lut[port] = ip_port;

    port++;
  }

  // Create the receive thread for the TUN interface
  {
    auto uth =
      [&tun_interface, &outqueue, &port_msg_lut, timeout_us] () {
        tun_raw_thread(tun_interface, outqueue, port_msg_lut, timeout_us);
      };
    thrs.push_back(std::shared_ptr<std::thread>(new std::thread(uth)));
  }

  // Create the TUN output thread.
  {
    auto tun_send_thr = std::shared_ptr<std::thread>
      (new std::thread([&tun_interface, send_queue]() {
                         while (1) {
                           Packet msg = send_queue->pop();
                           tun_interface.write(msg->data(), msg->size());
                         }
                       }));
    thrs.push_back(tun_send_thr);
  }

  // Create threads for sending and receiving status packets.
  PacketQueueP log_out(new PacketQueue(1000));
  PacketQueueP packed_log_out(new PacketQueue(1000));
  PacketQueueP log_in(new PacketQueue(1000));
  LOG_INFO << "Sending status to: " << log_to_host << ":" << link_status_ip_port;
  auto logts = [log_out, log_to_host, link_status_ip_port] () {
                 udp_send_loop(log_out, log_to_host, link_status_ip_port);
                 };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(logts)));
  LOG_INFO << "Sending packed status to: " << local_host << ":" << packed_status_port;
  auto plogts = [packed_log_out, packed_status_host, packed_status_port] () {
                  udp_send_loop(packed_log_out, packed_status_host, packed_status_port);
                };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(plogts)));
  LOG_INFO << "Receiving status packets at on port: " << link_status_ip_port;
  auto logrx = [log_in, link_status_ip_port, blocksize] () {
                 udp_recv_loop(log_in, "0.0.0.0", link_status_ip_port, blocksize);
               };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(logrx)));

  // Create the stats logging thread.
  {
    auto logth = [&trans_stats, &trans_stats_other, syslog_period, status_period,
                  log_out, packed_log_out, log_in, port_lut] {
                   log_thread(trans_stats, trans_stats_other, syslog_period, status_period,
                              log_out, packed_log_out, log_in, port_lut);
                 };
    thrs.push_back(std::shared_ptr<std::thread>(new std::thread(logth)));
  }

  // Create the thread for FEC decode and distributing the messages from incoming raw socket queue
  auto usth = [&inqueue, send_queue, &trans_stats, &trans_stats_other, link_status_port] () {
                fec_decode_thread(inqueue, send_queue, trans_stats);
              };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(usth)));

  // Keep trying to connect/reconnect to the devices
  std::set<std::string> current_devices;
  std::string transmit_device;
  std::string receive_device;
  std::shared_ptr<std::thread> send_thread;
  std::shared_ptr<std::thread> recv_thread;
  std::shared_ptr<std::thread> relay_thread;
  std::shared_ptr<SharedQueue<std::shared_ptr<monitor_message_t> > > relay_queue;
  while (1) {
    reset_wifi = false;

    // Query the list of devices until we detect a change
    const UDevInterface::DeviceList &devices = udev.devices();
    bool change = (devices.size() != current_devices.size());
    if (!change) {
      for (const auto &device : devices) {
        if (current_devices.find(device.first) == current_devices.end()) {
          change = true;
          break;
        }
      }
    }
    if (!change) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    // Terminate all the current send/receive threads
    reset_wifi = true;
    if (recv_thread) {
      recv_thread->join();
      recv_thread.reset();
    }
    if (send_thread) {
      outqueue.push(std::shared_ptr<Message>(new Message()));
      send_thread->join();
      send_thread.reset();
    }
    if (relay_thread) {
      relay_queue->push(std::shared_ptr<monitor_message_t>(new monitor_message_t));
      relay_thread->join();
      relay_thread.reset();
    }
    transmit_device.clear();
    receive_device.clear();
    reset_wifi = false;

    // Update the device list
    current_devices.clear();
    for (const auto &dev : devices) {
      current_devices.insert(dev.first);
    }

    // Configure the transmit threads as appropriate
    for (const auto &dev : devices) {
      std::string device = dev.first;
      std::string device_type = dev.second;

      // Configure that device if appropriate for this mode
      bool mcs, stbc, ldpc;
      std::string dev_mode = mode;
      if (!configure_device(device, device_type, true, false, conf,  mcs, stbc, ldpc, dev_mode,
                            do_configure)) {
        continue;
      }

      // Create the raw socket
      std::shared_ptr<RawSendSocket> raw_send_sock(new RawSendSocket(dev_mode == "ground", mtu));

      // Connect to the raw wifi interfaces.
      if (raw_send_sock->add_device(device, true, mcs, stbc, ldpc)) {
        LOG_INFO << "Transmitting on interface: " << device << " of type " << device_type;
      } else {
        LOG_ERROR << "Error opening the raw socket for transmiting: " << device
                  << " of type " << device_type;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }

      // Create a thread to send raw socket packets.
      auto send_th =
        [&outqueue, raw_send_sock, max_queue_size, &reset_wifi, &trans_stats]() {
          raw_send_thread(outqueue, *raw_send_sock, max_queue_size, trans_stats, reset_wifi);
          LOG_INFO << "Raw socket transmit thread exiting";
        };
      send_thread.reset(new std::thread(send_th));
      transmit_device = device;
      break;
    }

    // Configure the relay threads as appropriate
    for (const auto &dev : devices) {
      std::string device = dev.first;
      std::string device_type = dev.second;

      // Configure that device if appropriate for this mode
      bool mcs, stbc, ldpc;
      std::string dev_mode = mode;
      if (!configure_device(device, device_type, false, true, conf,  mcs, stbc, ldpc, dev_mode,
                            do_configure)) {
        continue;
      }

      // Create the raw socket
      std::shared_ptr<RawSendSocket> raw_send_sock(new RawSendSocket(dev_mode == "ground", mtu));

      // Connect to the raw wifi interfaces.
      if (raw_send_sock->add_device(device, true, mcs, stbc, ldpc)) {
        LOG_INFO << "Relaying on interface: " << device << " of type " << device_type;
      } else {
        LOG_ERROR << "Error opening the raw socket for relaying: " << device
                  << " of type " << device_type;
        continue;
      }

      // Create a thread to send raw socket packets.
      relay_queue = std::shared_ptr<SharedQueue<std::shared_ptr<monitor_message_t> > >
        (new SharedQueue<std::shared_ptr<monitor_message_t> >(max_queue_size));
      auto relay_th =
        [relay_queue, raw_send_sock, max_queue_size, &reset_wifi, &trans_stats]() {
          raw_relay_thread(relay_queue, *raw_send_sock, max_queue_size, reset_wifi);
          LOG_INFO << "Raw socket relay thread exiting";
        };
      relay_thread = std::make_shared<std::thread>(std::thread(relay_th));
      break;
    }

    // Configure receive device threads as appropriate
    for (const auto &dev : devices) {
      std::string device = dev.first;
      std::string device_type = dev.second;

      // Configure that device if appropriate for this mode
      bool mcs, stbc, ldpc;
      std::string dev_mode = mode;
      if (!configure_device(device, device_type, false, false, conf,  mcs, stbc, ldpc, mode,
                            do_configure)) {
        continue;
      }

      // Open the raw receive socket
      std::shared_ptr<RawReceiveSocket>
        raw_recv_sock(new RawReceiveSocket(dev_mode == "ground", mtu));
      if (raw_recv_sock->add_device(device)) {
        LOG_INFO << "Receiving on interface: " << device << " of type " << device_type;
      } else {
        LOG_ERROR << "Error opening the raw socket for receiving: " << device
                  << " of type " << device_type;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }

      // Create the raw socket receive thread
      auto recv =
        [raw_recv_sock, &inqueue, &reset_wifi, &trans_stats, &trans_stats_other, relay_queue,
         timeout_us]() {
          while(!reset_wifi) {
            std::shared_ptr<monitor_message_t> msg(new monitor_message_t);
            if (raw_recv_sock->receive(*msg, std::chrono::microseconds(timeout_us))) {

              // Did we stop receiving packets?
              if (!msg->data.empty()) {
                inqueue.push(msg);
                if (relay_queue) {
                  relay_queue->push(msg);
                }
                last_packet_time = cur_time();
              }
            } else {
              // Error return, which likely means the wifi card went away
              reset_wifi = true;
            }
          }
          LOG_INFO << "Raw socket receive thread exiting";
        };
      recv_thread.reset(new std::thread(recv));
      receive_device = device;
      break;
    }
  }

  return EXIT_SUCCESS;
}

std::set<std::string> parse_device_list(const INIReader &conf, const std::string &field) {
  std::string val = conf.Get("devices", field, "");
  std::vector<std::string> split;
  splitstr(val, split, ',');
  std::set<std::string> ret;
  for (auto &s : split) {
    ret.insert(s);
  }
  return ret;
}

bool configure_device(const std::string &device, const std::string &device_type, bool transmit,
                      bool relay, const INIReader &conf, bool &mcs, bool &stbc, bool &ldpc,
                      std::string &mode, bool do_configure) {
  // Ensure the device type is proper for the requested configuration
  std::string type = conf.Get("device-" + device, "type", "unspecified");
  bool ignore = (type == "ignore");
  bool is_relay = (type == "relay");
  bool is_transmitter = (type == "transmitter");
  bool is_receiver = (type == "receiver");
  bool is_unspecified = (type == "unspecified");
  if (ignore ||
      (transmit && (is_receiver || is_relay)) ||
      (!transmit && (is_transmitter || (is_relay && !relay))) ||
      (relay && !is_relay)) {
    return false;
  }
  // Lookup the specific device configuration first
  uint32_t freq = conf.GetInteger("device-" + device, "frequency", 0);
  if (freq == 0) {
    freq = conf.GetInteger("device-" + device_type, "frequency", 2412);
  }
  uint32_t txpower = conf.GetInteger("device-" + device, "txpower", 0);
  if (txpower == 0) {
    txpower = conf.GetInteger("device-" + device_type, "txpower", 20);
  }
  int datarate = conf.GetInteger("device-" + device, "datarate", -1);
  if (datarate == -1) {
    datarate = conf.GetInteger("device-" + device_type, "datarate", -1);
  }
  int mcs_mode = conf.GetInteger("device-" + device, "mcs", -1);
  if (mcs_mode == -1) {
    mcs_mode = conf.GetInteger("device-" + device_type, "mcs", 0);
  }
  mcs = (mcs_mode == 1);
  int stbc_mode = conf.GetInteger("device-" + device, "stbc", -1);
  if (stbc_mode == -1) {
    stbc_mode = conf.GetInteger("device-" + device_type, "stbc", 0);
  }
  stbc = (stbc_mode == 1);
  int ldpc_mode = conf.GetInteger("device-" + device, "ldpc", -1);
  if (ldpc_mode == -1) {
    ldpc_mode = conf.GetInteger("device-" + device_type, "ldpc", 0);
  }
  ldpc = (ldpc_mode == 1);
  std::string get_mode = conf.Get("device-" + device, "mode", "");
  if (!get_mode.empty()) {
    mode = get_mode;
  }
  LOG_INFO << "Configuring " << device << " (type=" << type << ") in monitor mode at frequency "
           << freq << " MHz  (txpower=" << txpower << ",datarate=" << datarate
           << ",mcs=" << mcs_mode << ",stbc=" << stbc_mode
           << ",ldpc=" << ldpc_mode << ",mode=" << mode << ")";

  // First configure the datarate if necessary
  if ((datarate >= 0) && (datarate < 7)) {
    constexpr uint8_t mcs_legacy_datarates[] = { 11, 22, 36, 48, 72, 96, 108 };
    uint8_t bitrate = mcs_legacy_datarates[datarate];

    // The interface must be up to change the bitrates
    if (!set_wifi_up(device)) {
      LOG_ERROR << "Unable to set bring " << device << " up";
      return false;
    }
    if (!set_wifi_legacy_bitrate(device, bitrate)) {
      LOG_WARNING << "Unable to set legacy datarate on " << device << " to (mcs="
                  << datarate << ") " << bitrate;
    }
    set_wifi_down(device);
  }

  // Configure the interface in monitor mode and bring the interface up
  if (!set_wifi_monitor_mode(device)) {
    LOG_ERROR << "Error trying to configure " << device << " to monitor mode";
    return false;
  }

  // Set the frequency
  if (!set_wifi_frequency(device, freq)) {
    LOG_ERROR << "Error setting frequency of " << device << " to " << freq;
    return false;
  }

  // Configure the transmit power
  if (!set_wifi_txpower(device, txpower)) {
    LOG_WARNING << "Unable to set txpower level on " << device << " to " << txpower;
  }

  return true;
}
