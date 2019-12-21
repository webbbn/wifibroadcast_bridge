
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
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <set>
#include <cstdlib>

#include <boost/program_options.hpp>

#include <boost/foreach.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <logging.hh>
#include <shared_queue.hh>
#include <raw_socket.hh>
#include <fec.hh>
#include <transfer_stats.hh>
#include <udp_send.hh>
#include <udp_receive.hh>
#include <wfb_bridge.hh>
#include <raw_send_thread.hh>

namespace po=boost::program_options;
namespace pt=boost::property_tree;


int main(int argc, const char** argv) {
  auto uid = getuid();
  if (uid != 0) {
    std::cerr << "This application must be run as root or with root privileges.\n";
    return EXIT_FAILURE;
  }

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "produce help message")
    ;

  std::string conf_file;
  po::options_description pos("Positional");
  pos.add_options()
    ("conf_file", po::value<std::string>(&conf_file),
     "the path to the configuration file used for configuring ports")
    ;
  po::positional_options_description p;
  p.add("conf_file", 1);

  po::options_description all_options("Allowed options");
  all_options.add(desc).add(pos);
  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).
	    options(all_options).positional(p).run(), vm);
  po::notify(vm);

  if (vm.count("help") || !vm.count("conf_file")) {
    std::cout << "Usage: options_description [options] <configuration file>\n";
    std::cout << desc << std::endl;
    return EXIT_SUCCESS;
  }

  // Parse the configuration file.
  pt::ptree conf;
  try {
    pt::read_ini(conf_file, conf);
  } catch(...) {
    std::cerr << "Error reading the configuration file: " << conf_file << std::endl;;
    return EXIT_FAILURE;
  }

  // Read the global parameters
  std::string mode = conf.get<std::string>("global.mode", "air");
  std::string log_level = conf.get<std::string>("global.loglevel", "info");
  std::string syslog_level = conf.get<std::string>("global.sysloglevel", "info");
  std::string syslog_host = conf.get<std::string>("global.sysloghost", "localhost");

  uint16_t max_queue_size = conf.get<uint16_t>("global.maxqueuesize", 200);

  // Create the logger
  Logger::create(log_level, syslog_level, syslog_host);
  LOG_INFO << "wfb_bridge running in " << mode << " mode and logging '"
	   << log_level << "' to console and '"
	   << syslog_level << "' to syslog";

  // Create the message queues.
  SharedQueue<std::shared_ptr<monitor_message_t> > inqueue;   // Wifi to UDP
  SharedQueue<std::shared_ptr<Message> > outqueue;  // UDP to Wifi

  // Maintain a list of all the threads
  std::vector<std::shared_ptr<std::thread> > thrs;

  // Maintain statistics for out side of the connection and the other side.
  // Each side shares stats via periodic messaegs.
  TransferStats trans_stats(mode);
  TransferStats trans_stats_other((mode == "air") ? "ground" : "air");

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
  BOOST_FOREACH(const auto &v, conf) {
    const std::string &group = v.first;

    // Ignore global options.
    if (group == "global") {
      continue;
    }

    // Only process uplink configuration entries.
    std::string direction = v.second.get<std::string>("direction", "");
    if (((direction == "up") && (mode == "air")) ||
	((direction == "down") && (mode == "ground"))) {

      // Get the name.
      std::string name = v.second.get<std::string>("name", "");

      // Get the UDP port number or output filename (required).
      uint16_t outport = v.second.get<uint16_t>("outport", 0);
      std::string outfile;
      if (outport == 0) {
	outfile = v.second.get<std::string>("outfile", "");
	if (outfile == "") {
	  LOG_CRITICAL << "No outport or outfile specified for " << name;
	  return EXIT_FAILURE;
	}
      }

      // Get the remote hostname/ip (optional)
      std::string hostname = v.second.get<std::string>("outhost", "127.0.0.1");

      // Get the port number (required).
      uint8_t port = v.second.get<uint16_t>("port", 0);
      if (port > 15) {
	LOG_CRITICAL << "Invalid port specified for " << name << "  (" << port << ")";
	return EXIT_FAILURE;
      }

      // Get the link type
      std::string type = v.second.get<std::string>("type", "data");

      // Create the FEC decoder if requested.
      std::shared_ptr<FECDecoder> dec(new FECDecoder());
      if ((port > 0) && (outport > 0)) {
	udp_out[port].reset(new UDPDestination(outport, hostname, dec));
	// Is this the port that will receive status from the other side?
	udp_out[port]->is_status = ((group == "status_down") || (group == "status_up"));
      } else if (port > 0) {
	udp_out[port].reset(new UDPDestination(outfile, dec));
      }
    }
  }

  // Create the thread for retrieving messages from incoming raw socket queue
  // and send the UDP packets.
  auto usth = [&inqueue, &udp_out, send_sock, &trans_stats, &trans_stats_other]() {
    udp_send_loop(inqueue, udp_out, send_sock, trans_stats, trans_stats_other);
  };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(usth)));

  // Interfaces can come and go, so we need to loop until an interface comes up
  // and deal with interfaces going down.
  while (1) {
    bool terminate = false;
    LOG_DEBUG << "Detecting network interfaces";

    // Get a list of the network devices.
    std::vector<std::string> ifnames;
    if (!detect_network_devices(ifnames)) {
      LOG_CRITICAL << "Error reading the network interfaces.";
      return EXIT_FAILURE;
    }
    if (ifnames.empty()) {
      sleep(1);
      continue;
    }
    LOG_DEBUG << "Network interfaces found: ";
    for (const auto &ifname : ifnames) {
      LOG_DEBUG << "  " << ifname;
    }
    // Give things time to settle before start trying to connect
    sleep(2);

    // Open the raw transmit socket
    RawSendSocket raw_send_sock((mode == "ground"));
    // Connect to the raw wifi interfaces.
    bool valid_send_sock = false;
    for (const auto &device : ifnames) {
      if (raw_send_sock.add_device(device, true)) {
	valid_send_sock = true;
	LOG_DEBUG << "Transmitting on interface: " << device;
	break;
      }
    }
    if (!valid_send_sock) {
      LOG_DEBUG << "Error opeing the raw socket for transmiting.";
      sleep(1);
      continue;
    }

    // Create a thread to send raw socket packets.
    auto send_th =
      [&outqueue, &raw_send_sock, max_queue_size, &terminate, &trans_stats]() {
      raw_send_thread(outqueue, raw_send_sock, max_queue_size, trans_stats, terminate);
    };
    std::thread send_thread(send_th);

    // Open the raw receive socket
    RawReceiveSocket raw_recv_sock((mode == "ground"));
    bool valid_recv_sock = false;
    for (const auto &device : ifnames) {
      LOG_DEBUG << "Trying to configure device: " << device;
      if (raw_recv_sock.add_device(device)) {
	valid_recv_sock = true;
	LOG_INFO << "Receiving on interface: " << device;
	break;
      } else {
	LOG_DEBUG << "Unable to configure wifi device: " << device;
      }
    }

    // Create the raw socket receive thread
    auto recv =
      [&raw_recv_sock, &inqueue, &terminate, &trans_stats, &trans_stats_other]() {
	//uint16_t recv_timeout_ms = 2000;
	//uint16_t recv_down_time = 0;
	while(!terminate) {
	  std::shared_ptr<monitor_message_t> msg(new monitor_message_t);
	  if (raw_recv_sock.receive(*msg, std::chrono::milliseconds(200))) {
	    inqueue.push(msg);
	    //recv_down_time = 0;
	  } else {
	    trans_stats.timeout();
	    trans_stats_other.timeout();
	    //recv_down_time += 200;
	  }
	  //if (recv_down_time >= recv_timeout_ms) {
	  //terminate = true;
	  //}
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
