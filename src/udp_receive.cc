
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <logging.hh>
#include <udp_send.hh>
#include <udp_receive.hh>

int open_udp_socket_for_rx(uint16_t port, const std::string hostname, uint32_t timeout_us) {

  // Try to open a UDP socket.
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    LOG_ERROR << "Error opening the UDP receive socket.";
    return -1;
  }

  // Set the socket options.
  int optval = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

  // Set a timeout to ensure that the end of a frame gets flushed
  if (timeout_us > 0) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_us;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
  }

  // Find to the receive port
  struct sockaddr_in saddr;
  bzero((char *)&saddr, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);

  // Lookup the IP address from the hostname
  std::string ip;
  if (hostname != "") {
    ip = hostname_to_ip(hostname);
    saddr.sin_addr.s_addr = inet_addr(ip.c_str());
  } else {
    saddr.sin_addr.s_addr = INADDR_ANY;
  }

  if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    LOG_ERROR << "Error binding to the UDP receive socket: " << port;
    return -1;
  }

  return fd;
}


bool create_udp_to_raw_threads(SharedQueue<std::shared_ptr<Message> > &outqueue,
			       std::vector<std::shared_ptr<std::thread> > &thrs,
			       boost::property_tree::ptree &conf,
			       TransferStats &trans_stats,
			       TransferStats &trans_stats_other,
			       const std::string &mode) {

  // Extract a couple of global options.
  float syslog_period = conf.get<float>("global.syslogperiod", 5);
  float status_period = conf.get<float>("global.statusperiod", 0.2);

  // If this is the ground side, get the host and port to send status messages to.
  std::string status_host;
  uint16_t status_port = 0;
  std::string packed_status_host;
  uint16_t packed_status_port = 0;
  if (mode == "ground") {
    status_host = conf.get<std::string>("status_down.outhost", "");
    status_port = conf.get<uint16_t>("status_down.outport", 0);
    LOG_INFO << "Sending status to udp://" << status_host << ":" << status_port;
    packed_status_host = conf.get<std::string>("packed_status_down.outhost", "");
    packed_status_port = conf.get<uint16_t>("packed_status_down.outport", 0);
    LOG_INFO << "Sending packed status to udp://" << packed_status_host << ":" << packed_status_port;
  } else {
    status_host = conf.get<std::string>("status_up.outhost", "");
    status_port = conf.get<uint16_t>("status_up.outport", 0);
    LOG_INFO << "Sending status to udp://" << status_host << ":" << status_port;
  }

  // Create the the threads for receiving packets from UDP sockets
  // and relaying them to the raw socket interface.
  for (const auto &v : conf) {
    const std::string &group = v.first;

    // Ignore global options and the output-only status port
    if ((group == "global") || (group == "packed_status_down")) {
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
	LOG_CRITICAL << "No inport specified for " << group;
	return false;
      }

      // Get the remote hostname/ip (optional)
      std::string hostname = v.second.get<std::string>("inhost", "127.0.0.1");

      // Get the port number (required).
      uint8_t port = v.second.get<uint16_t>("port", 0);
      if (port == 0) {
	LOG_CRITICAL << "No port specified for " << group;
	return false;
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
		      &outqueue, msg, status_host, status_port,
		      packed_status_host, packed_status_port]() {
	  std::shared_ptr<UDPDestination> udp_out
	  (new UDPDestination(status_port, status_host, std::shared_ptr<FECDecoder>()));
	  std::shared_ptr<UDPDestination> packed_udp_out;
	  if ((packed_status_host != "") && (packed_status_port != 0)) {
	    packed_udp_out.reset(new UDPDestination(packed_status_port, packed_status_host,
						    std::shared_ptr<FECDecoder>()));
	  }
	  log_thread(trans_stats, trans_stats_other, syslog_period, status_period, outqueue, msg,
		     udp_out, packed_udp_out);
	};
	thrs.push_back(std::shared_ptr<std::thread>(new std::thread(logth)));

      } else {

	// Try to open the UDP socket.
	uint32_t timeout_us = do_fec ? 1000 : 0; // 1ms timeout for FEC links to support flushing
	int udp_sock = open_udp_socket_for_rx(inport, hostname, timeout_us);
	if (udp_sock < 0) {
	  LOG_CRITICAL << "Error opening the UDP socket for " << name << "  ("
		       << hostname << ":" << port << ")";
	  return false;
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

  return true;
}
