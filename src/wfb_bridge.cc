
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

#include <cxxopts.hpp>

#include <INIReader.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <set>
#include <cstdlib>

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
                      std::string &mode);


int main(int argc, char** argv) {

  // Ensure we've running with root privileges
  auto uid = getuid();
  if (uid != 0) {
    std::cerr << "This application must be run as root or with root privileges.\n";
    return EXIT_FAILURE;
  }

  std::string conf_file;
  cxxopts::Options options(argv[0], "Allowed options");
  options.add_options()
    ("h,help", "produce help message")
    ("conf_file", "the path to the configuration file used for configuring ports",
     cxxopts::value<std::string>(conf_file))
    ;
  options.parse_positional({"conf_file"});
  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("conf_file")) {
    std::cout << options.help() << std::endl;
    std::cout << "Positional parameters: <configuration file>\n";
    return EXIT_SUCCESS;
  }

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

  uint16_t max_queue_size = static_cast<uint16_t>(conf.GetInteger("global", "maxqueuesize", 200));

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

  // Create the message queues.
  SharedQueue<std::shared_ptr<monitor_message_t> > inqueue(max_queue_size);   // Wifi to UDP
  SharedQueue<std::shared_ptr<Message> > outqueue(max_queue_size);  // UDP to Wifi

  // Maintain a list of all the threads
  std::vector<std::shared_ptr<std::thread> > thrs;

  // Maintain statistics for out side of the connection and the other side.
  // Each side shares stats via periodic messaegs.
  TransferStats trans_stats(mode);
  TransferStats trans_stats_other((mode == "air") ? "ground" : "air");

  // Create an interface to udev that monitors for wifi card insertions / deletions
  bool reset_wifi = false;
  UDevInterface udev(reset_wifi);
  thrs.push_back(std::make_shared<std::thread>(std::thread(&UDevInterface::monitor_thread, &udev)));

  // Open the UDP send socket
  int send_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (send_sock < 0) {
    LOG_CRITICAL << "Error opening the UDP send socket.";
    return EXIT_FAILURE;
  }
  int trueflag = 1;
  if (setsockopt(send_sock, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof(trueflag)) < 0) {
    LOG_CRITICAL << "Error setting the UDP send socket to broadcast.";
    return EXIT_FAILURE;
  }

  // Create the interfaces to FEC decoders and send out the blocks received off the raw socket.
  PacketQueues udp_send_queues[MAX_PORTS];
  const std::set<std::string> &sections = conf.Sections();
  uint8_t status_port = 0;
  uint8_t packed_status_port = 0;
  for (const auto &group : sections) {

    // Ignore sections that don't start with 'link-'
    if (group.substr(0, 5) != "link-") {
      continue;
    }

    // Only process uplink configuration entries.
    std::string direction = conf.Get(group, "direction", "");
    if (((direction == "up") && (mode == "air")) ||
	((direction == "down") && (mode == "ground"))) {

      // Get the name.
      std::string name = conf.Get(group, "name", "");

      // Get the port number (required).
      uint8_t port = static_cast<uint16_t>(conf.GetInteger(group, "port", 0));
      if (port > MAX_PORTS) {
	LOG_CRITICAL << "Invalid port specified for " << name << "  (" << port << ")";
	return EXIT_FAILURE;
      }

      // Get the remote hostname/ip(s) and port(s)
      std::string outports_str = conf.Get(group, "outports", "");
      std::vector<std::string> outports;
      splitstr(outports_str, outports, ',');
      bool is_status = false;
      bool is_packed_status = false;
      if ((group == "link-status_down") || (group == "link-status_up")) {
        status_port = port;
        is_status = true;
      }
      if ((group == "link-packed_status_down") || (group == "link-packed_status_up")) {
        packed_status_port = port;
        is_packed_status = true;
      }

      // Create a file logger if requested
      std::string archive_dir = conf.Get(group, "archive_outdir", "");
      if (!archive_dir.empty()) {
        PacketQueueP q(new PacketQueue(1000));
        udp_send_queues[port].push_back(q);
        // Spawn the archive thread.
        std::shared_ptr<std::thread> archive_thread
          (new std::thread([q, port, archive_dir]() { archive_loop(archive_dir, q); }));
        thrs.push_back(archive_thread);
      }

      // Create the UDP output destinations.
      for (size_t i = 0; i < outports.size(); ++i) {
        std::vector<std::string> host_port;
        splitstr(outports[i], host_port, ':');
        if (host_port.size() != 2) {
          LOG_CRITICAL << "Invalid host:port specified in group: "
                       << group << " (" << outports[i] << ")";
          return EXIT_FAILURE;
        }
        const std::string &hostname = host_port[0];
        uint16_t udp_port = atoi(host_port[1].c_str());
        if (is_status) {
          LOG_INFO << "Sending status to: " << hostname << ":" << udp_port;
        }
        if (is_packed_status) {
          LOG_INFO << "Sending packed status to: " << hostname << ":" << udp_port;
        }
        PacketQueueP q(new PacketQueue(max_queue_size, true));
        udp_send_queues[port].push_back(q);
        // Create a UDP send thread for this output port
        auto udp_send_thr = std::shared_ptr<std::thread>
          (new std::thread([hostname, udp_port, port, i, q]() {
                             udp_send_loop(q, hostname, udp_port);
                           }));
        thrs.push_back(udp_send_thr);
      }
    }
  }

  // Create the thread for FEC decode and distributin the messages from incoming raw socket queue
  auto usth = [&inqueue, &udp_send_queues, &trans_stats, &trans_stats_other, status_port]() {
                fec_decode_thread(inqueue, udp_send_queues, trans_stats, trans_stats_other,
                                  status_port);
              };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(usth)));

  // Create the UDP -> raw socket interfaces
  if (!create_udp_to_raw_threads(outqueue, thrs, conf, trans_stats, trans_stats_other,
                                 udp_send_queues[status_port], udp_send_queues[packed_status_port],
                                 mode, max_queue_size)) {
    return EXIT_FAILURE;
  }

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
      if (!configure_device(device, device_type, true, false, conf,  mcs, stbc, ldpc, dev_mode)) {
        continue;
      }

      // Create the raw socket
      std::shared_ptr<RawSendSocket> raw_send_sock(new RawSendSocket(dev_mode == "ground"));

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
      if (!configure_device(device, device_type, false, true, conf,  mcs, stbc, ldpc, dev_mode)) {
        continue;
      }

      // Create the raw socket
      std::shared_ptr<RawSendSocket> raw_send_sock(new RawSendSocket(dev_mode == "ground"));

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
      if (!configure_device(device, device_type, false, false, conf,  mcs, stbc, ldpc, mode)) {
        continue;
      }

      // Open the raw receive socket
      std::shared_ptr<RawReceiveSocket> raw_recv_sock(new RawReceiveSocket(dev_mode == "ground"));
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
        [raw_recv_sock, &inqueue, &reset_wifi, &trans_stats, &trans_stats_other, relay_queue]() {
          while(!reset_wifi) {
            std::shared_ptr<monitor_message_t> msg(new monitor_message_t);
            if (raw_recv_sock->receive(*msg, std::chrono::milliseconds(200))) {

              // Did we stop receiving packets?
              if (msg->data.empty()) {
                trans_stats.timeout();
                trans_stats_other.timeout();
              } else {
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
                      std::string &mode) {
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
  << freq << " MHz  (mcs=" << mcs_mode << ",stbc=" << stbc_mode
  << ",ldpc=" << ldpc_mode << ",mode=" << mode << ")";
  if (!set_wifi_monitor_mode(device)) {
    LOG_ERROR << "Error trying to configure " << device << " to monitor mode";
    return false;
  }
  if (!set_wifi_frequency(device, freq)) {
    LOG_ERROR << "Error setting frequency of " << device << " to " << freq;
    return false;
  }
  return true;
}
