
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


int main(int argc, char** argv) {

  // Ensure we've running with root privileges
  auto uid = getuid();
  if (uid != 0) {
    std::cerr << "This application must be run as root or with root privileges.\n";
    return EXIT_FAILURE;
  }

  std::string conf_file;
  std::string mode;
  cxxopts::Options options(argv[0], "Allowed options");
  options.add_options()
    ("h,help", "produce help message")
    ("conf_file", "the path to the configuration file used for configuring ports",
     cxxopts::value<std::string>(conf_file))
    ("mode", "the mode (air|ground)", cxxopts::value<std::string>(mode))
    ;
  options.parse_positional({"conf_file", "mode"});
  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("conf_file") ||
      !result.count("mode")) {
    std::cout << options.help() << std::endl;
    std::cout << "Positional parameters: <configuration file> <mode (air|ground)>\n";
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
  SharedQueue<std::shared_ptr<monitor_message_t> > inqueue;   // Wifi to UDP
  SharedQueue<std::shared_ptr<Message> > outqueue;  // UDP to Wifi

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

  // Create the UDP -> raw socket interfaces
  if (!create_udp_to_raw_threads(outqueue, thrs, conf, trans_stats, trans_stats_other, mode)) {
    return EXIT_FAILURE;
  }

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
  std::vector<std::shared_ptr<UDPDestination> > udp_out(64);
  const std::set<std::string> &sections = conf.Sections();
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

      // Get the remote hostname/ip(s) and port(s)
      std::string outports = conf.Get(group, "outports", "");

      // Get the port number (required).
      uint8_t port = static_cast<uint16_t>(conf.GetInteger(group, "port", 0));
      if (port > 64) {
	LOG_CRITICAL << "Invalid port specified for " << name << "  (" << port << ")";
	return EXIT_FAILURE;
      }

      // Create the FEC decoder if requested.
      std::shared_ptr<FECDecoder> dec(new FECDecoder());
      if (port > 0) {
	udp_out[port].reset(new UDPDestination(outports, dec));
	// Is this the port that will receive status from the other side?
	udp_out[port]->is_status((group == "status_down") || (group == "status_up"));
      }
    }
  }

  // Create the thread for retrieving messages from incoming raw socket queue
  // and send the UDP packets.
  auto usth = [&inqueue, &udp_out, send_sock, &trans_stats, &trans_stats_other]() {
    udp_send_loop(inqueue, udp_out, send_sock, trans_stats, trans_stats_other);
  };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(usth)));

  // Keep trying to connect/reconnect to the device
  while (1) {
    reset_wifi = false;

    // Just use the first device for now.
    const UDevInterface::DeviceList devices = udev.devices();
    if (devices.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    std::string device = devices[0].first;
    std::string device_type = devices[0].second;

    // Try to configure the interface
    uint32_t freq = conf.GetInteger("device-" + device_type, "frequency", 2412);
    LOG_INFO << "Configuring " << device << " in monitor mode at frequency " << freq
             << " MHz";
    if (!set_wifi_monitor_mode(device)) {
      LOG_ERROR << "Error trying to configure " << device << " to monitor mode";
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    if (!set_wifi_frequency(device, freq)) {
      LOG_ERROR << "Error setting frequency of " << device << " to " << freq;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    // Open the raw transmit socket
    bool mcs = conf.GetInteger("device-" + device_type, "mcs", 0) != 0;
    bool stbc = conf.GetInteger("device-" + device_type, "stbc", 0) != 0;
    bool ldpc = conf.GetInteger("device-" + device_type, "ldpc", 0) != 0;
    RawSendSocket raw_send_sock((mode == "ground"));
    // Connect to the raw wifi interfaces.
    if (raw_send_sock.add_device(device, true, mcs, stbc, ldpc)) {
      LOG_INFO << "Transmitting on interface: " << device << " of type " << device_type;
    } else {
      LOG_ERROR << "Error opening the raw socket for transmiting: " << device
                << " of type " << device_type;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    // Create a thread to send raw socket packets.
    auto send_th =
      [&outqueue, &raw_send_sock, max_queue_size, &reset_wifi, &trans_stats]() {
      raw_send_thread(outqueue, raw_send_sock, max_queue_size, trans_stats, reset_wifi);
    };
    std::thread send_thread(send_th);

    // Open the raw receive socket
    RawReceiveSocket raw_recv_sock((mode == "ground"));
    if (raw_recv_sock.add_device(device)) {
      LOG_INFO << "Receiving on interface: " << device << " of type " << device_type;
    } else {
      LOG_ERROR << "Error opening the raw socket for receiving: " << device
                << " of type " << device_type;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    // Create the raw socket receive thread
    auto recv =
      [&raw_recv_sock, &inqueue, &reset_wifi, &trans_stats, &trans_stats_other]() {
	while(!reset_wifi) {
	  std::shared_ptr<monitor_message_t> msg(new monitor_message_t);
	  if (raw_recv_sock.receive(*msg, std::chrono::milliseconds(200))) {

	    // Did we stop receiving packets?
	    if (msg->data.empty()) {
	      trans_stats.timeout();
	      trans_stats_other.timeout();
	    } else {
              inqueue.push(msg);
              last_packet_time = cur_time();
            }
	  } else {
	    // Error return, which likely means the wifi card went away
	    reset_wifi = true;
	  }
	}
	LOG_DEBUG << "Raw socket receive thread exiting";
    };
    std::thread recv_thread(recv);

    // Join on the send and receive threads, which should terminate if/when the device is removed.
    recv_thread.join();
    LOG_DEBUG << "Raw receive send thread has terminated.";
    // Force the send thread to terminate.
    outqueue.push(std::shared_ptr<Message>(new Message()));
    send_thread.join();
    LOG_DEBUG << "Raw socket send thread has terminated.";
  }

  return EXIT_SUCCESS;
}
