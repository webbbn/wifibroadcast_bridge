
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
#include <log_thread.hh>
#include <raw_send_thread.hh>

namespace po=boost::program_options;
namespace pt=boost::property_tree;


int main(int argc, const char** argv) {

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
  float syslog_period = conf.get<float>("global.syslogperiod", 5);
  float status_period = conf.get<float>("global.statusperiod", 0.2);

  uint16_t max_queue_size = conf.get<uint16_t>("global.maxqueuesize", 200);

  // Create the logger
  Logger::create(log_level, syslog_level, syslog_host);
  LOG_INFO << "wfb_bridge running in " << mode << " mode and logging '"
	   << log_level << "' to console and '"
	   << syslog_level << "' to syslog";

  // Create the message queues.
  SharedQueue<std::shared_ptr<monitor_message_t> > inqueue;   // Wifi to UDP
  SharedQueue<std::shared_ptr<Message> > outqueue;  // UDP to Wifi

  // If this is the ground side, get the host and port to send status messages to.
  std::string status_host;
  uint16_t status_port = 0;
  if (mode == "ground") {
    status_host = conf.get<std::string>("status_down.outhost", "");
    status_port = conf.get<uint16_t>("status_down.outport", 0);
    LOG_INFO << "Sending status to udp://" << status_host << ":" << status_port;
  } else {
    status_host = conf.get<std::string>("status_up.outhost", "");
    status_port = conf.get<uint16_t>("status_up.outport", 0);
    LOG_INFO << "Sending status to udp://" << status_host << ":" << status_port;
  }

  // Create the the threads for receiving blocks off the UDP sockets
  // and relaying them to the raw socket interface.
  std::vector<std::shared_ptr<std::thread> > thrs;
  TransferStats trans_stats(mode);
  TransferStats trans_stats_other((mode == "air") ? "ground" : "air");
  BOOST_FOREACH(const auto &v, conf) {
    const std::string &group = v.first;

    // Ignore global options.
    if (group == "global") {
      continue;
    }

    // Only process uplink configuration entries.
    std::string direction = v.second.get<std::string>("direction", "");
    if (((direction == "down") && (mode == "air")) ||
	((direction == "up") && (mode == "ground"))) {

      // Get the name.
      std::string name = v.second.get<std::string>("name", "");

      // Get the UDP port number (required except for status).
      uint16_t inport = v.second.get<uint16_t>("inport", 0);
      if ((inport == 0) && (group != "status_down") && (group != "status_up")) {
	LOG_CRITICAL << "No inport specified for " << name;
	return EXIT_FAILURE;
      }

      // Get the remote hostname/ip (optional)
      std::string hostname = v.second.get<std::string>("inhost", "127.0.0.1");

      // Get the port number (required).
      uint8_t port = v.second.get<uint16_t>("port", 0);
      if (port == 0) {
	LOG_CRITICAL << "No port specified for " << name;
	return EXIT_FAILURE;
      }

      // Get the link type
      std::string type = v.second.get<std::string>("type", "data");

      // Get the priority (optional).
      uint8_t priority = v.second.get<uint8_t>("priority", 100);

      // Get the FEC stats (optional).
      uint16_t blocksize = v.second.get<uint16_t>("blocksize", 1500);
      uint8_t nblocks = v.second.get<uint8_t>("blocks", 1);
      uint8_t nfec_blocks = v.second.get<uint8_t>("fec", 0);
      bool do_fec = ((nblocks > 0) && (nfec_blocks > 0));

      // Get the Tx parameters (optional).
      WifiOptions opts;
      opts.data_rate = v.second.get<uint8_t>("datarate", 18);
      opts.mcs = v.second.get<uint8_t>("mcs", 0) ? true : false;
      opts.stbc = v.second.get<uint8_t>("stbc", 0) ? true : false;
      opts.ldpc = v.second.get<uint8_t>("ldpc", 0) ? true : false;

      // Allocate the encoder
      std::shared_ptr<FECEncoder> enc(new FECEncoder(nblocks, nfec_blocks, blocksize));

      // Create the FEC encoder if requested.
      if (type == "data"){
	opts.link_type = DATA_LINK;
      } else if (type == "short") {
	opts.link_type = SHORT_DATA_LINK;
      } else if (type == "rts") {
	opts.link_type = RTS_DATA_LINK;
      } else {
	opts.link_type = DATA_LINK;
      }

      // Create the logging thread if this is a status down channel.
      if ((group == "status_down") || (group == "status_up")) {

	// Create the stats logging thread.
	std::shared_ptr<Message> msg(new Message(blocksize, port, priority, opts, enc));
	auto logth = [&trans_stats, &trans_stats_other, syslog_period, status_period,
		      &outqueue, msg, status_host, status_port]() {
	  std::shared_ptr<UDPDestination> udp_out
	  (new UDPDestination(status_port, status_host, std::shared_ptr<FECDecoder>()));
	  log_thread(trans_stats, trans_stats_other, syslog_period, status_period, outqueue, msg,
		     udp_out);
	};
	thrs.push_back(std::shared_ptr<std::thread>(new std::thread(logth)));

      } else {

	// Try to open the UDP socket.
	uint32_t timeout_us = do_fec ? 1000 : 0; // 1ms timeout for FEC links to support flushing
	int udp_sock = open_udp_socket_for_rx(inport, hostname, timeout_us);
	if (udp_sock < 0) {
	  LOG_CRITICAL << "Error opening the UDP socket for " << name << "  ("
		       << hostname << ":" << port;
	  return EXIT_FAILURE;
	}

	// Create the receive thread for this socket
	auto uth = [udp_sock, port, enc, opts, priority, blocksize, &outqueue, inport]() {
	  bool flushed = false;
	  uint32_t cntr = 0;
	  while (1) {
	    std::shared_ptr<Message> msg(new Message(blocksize, port, priority, opts, enc));
	    ssize_t count = recv(udp_sock, msg->msg.data(), blocksize, 0);
	    if (count < 0) {
	      if (!flushed) {
		// Indicate a flush by putting an empty message on the queue
		count = 0;
		flushed = true;
	      } else {
		continue;
	      }
	    } else {
	      flushed = false;
	    }
	    if (count > 0) {
	      msg->msg.resize(count);
	      outqueue.push(msg);
	    }
	  }
	};
	thrs.push_back(std::shared_ptr<std::thread>(new std::thread(uth)));
      }
    }    
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
      if (port == 0) {
	LOG_CRITICAL << "No port specified for " << name;
	return EXIT_FAILURE;
      }
      if (port > 15) {
	LOG_CRITICAL << "Invalid port specified for " << name << "  (" << port << ")";
	return EXIT_FAILURE;
      }

      // Get the link type
      std::string type = v.second.get<std::string>("type", "data");

      // Get the FEC stats (optional).
      uint16_t blocksize = v.second.get<uint16_t>("blocksize", 1500);
      uint8_t nblocks = v.second.get<uint8_t>("blocks", 0);
      uint8_t nfec_blocks = v.second.get<uint8_t>("fec", 0);

      // Create the FEC decoder if requested.
      std::shared_ptr<FECDecoder> dec(new FECDecoder());

      // Is this the special status port?
      if ((group == "status_down") || (group == "status_up")) {
	status_port = port;
      }

      if (outport > 0) {
	udp_out[port].reset(new UDPDestination(outport, hostname, dec));
      } else {
	udp_out[port].reset(new UDPDestination(outfile, dec));
      }
    }
  }

  // Create the thread for retrieving messages from incoming raw socket queue
  // and send the UDP packets.
  auto usth = [&inqueue, &udp_out, send_sock, &trans_stats, &trans_stats_other, status_port]() {
    udp_send_loop(inqueue, udp_out, send_sock, status_port,
		  trans_stats, trans_stats_other);
  };
  thrs.push_back(std::shared_ptr<std::thread>(new std::thread(usth)));

  // Interfaces can come and go, so we need to loop until an interface comes up
  // and deal with interfaces going down.
  std::vector<std::string> ifnames;
  while (1) {
    bool terminate = false;
    LOG_DEBUG << "Detecting network interfaces";

    // Get a list of the network devices.
    ifnames.clear();
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

    // Open the raw transmit socket
    RawSendSocket raw_send_sock((mode == "ground"));
    // Connect to the raw wifi interfaces.
    bool valid_send_sock = false;
    for (const auto &device : ifnames) {
      if (raw_send_sock.add_device(device)) {
	valid_send_sock = true;
	LOG_INFO << "Transmitting on interface: " << device;
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
	LOG_WARNING << "Unable to configure wifi device: " << device;
      }
    }

    // Create the raw socket receive thread
    auto recv =
      [&raw_recv_sock, &inqueue, &terminate]() {
	while(!terminate) {
	  std::shared_ptr<monitor_message_t> msg(new monitor_message_t);
	  if (raw_recv_sock.receive(*msg)) {
	    inqueue.push(msg);
	  }
	}
	LOG_INFO << "Raw socket receive thread exiting";
    };
    std::thread recv_thread(recv);

    // Join on the send and receive threads, which should terminate if/when the device is removed.
    recv_thread.join();
    LOG_WARNING << "Raw receive send thread has terminated.";
    // Force the send thread to terminate.
    outqueue.push(std::shared_ptr<Message>(new Message()));
    send_thread.join();
    LOG_WARNING << "Raw socket send thread has terminated.";
  }

  return EXIT_SUCCESS;
}
