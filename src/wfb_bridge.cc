
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
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <logging.hh>
#include <stats_accumulator.hh>
#include <shared_queue.hh>
#include <raw_socket.hh>
#include <fec.hh>

namespace po=boost::program_options;
namespace pt=boost::property_tree;

std::string hostname_to_ip(const std::string &hostname);


/************************************************************************************************
 * Class definitions
 ************************************************************************************************/

class TransferStats {
public:

  TransferStats(const std::string &name) :
    m_name(name), m_seq(0), m_blocks(0), m_bytes(0), m_block_errors(0), m_seq_errors(0),
    m_send_bytes(0), m_send_blocks(0), m_inject_errors(0), m_flushes(0) {}

  void add(const FECDecoderStats &cur, const FECDecoderStats &prev) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_seq += cur.total_blocks - prev.total_blocks;
    m_blocks += cur.total_packets - prev.total_packets;
    m_bytes += cur.bytes - prev.bytes;
    m_block_errors += cur.dropped_packets - prev.dropped_packets;
    m_seq_errors += cur.dropped_blocks - prev.dropped_blocks;
  }
  void add_rssi(int8_t rssi) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rssi.add(rssi);
  }

  void add_send_stats(uint32_t bytes, uint32_t nblocks, uint16_t inject_errors, uint32_t queue_size,
		      bool flush, float pkt_time) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_send_bytes += bytes;
    m_send_blocks += nblocks;
    m_inject_errors += inject_errors;
    m_queue_size.add(queue_size);
    m_flushes += (flush ? 1 : 0);
    m_pkt_time.add(pkt_time);
  }

  void add_encode_time(double t) {
    m_enc_time.add(t);
  }

  void add_send_time(double t) {
    m_send_time.add(t);
  }

  transfer_stats_t get_stats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    transfer_stats_t stats;
    stats.sequences = m_seq;
    stats.blocks_in = m_blocks;
    stats.blocks_out = m_send_blocks;
    stats.bytes_in = m_bytes;
    stats.bytes_out = m_send_bytes;
    stats.encode_time = m_enc_time.mean(1e6);
    stats.send_time = m_send_time.mean(1e6);
    stats.pkt_time = m_pkt_time.mean(1e6);
    stats.sequence_errors = m_seq_errors;
    stats.block_errors = m_block_errors;
    stats.inject_errors = m_inject_errors;
    stats.rssi = m_rssi.mean();
    stats.rssi_min = m_rssi.min();
    stats.rssi_max = m_rssi.max();
    return stats;
  }

  void reset_accumulators() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue_size.reset();
    m_enc_time.reset();
    m_send_time.reset();
    m_pkt_time.reset();
    m_rssi.reset();
  }

  void update(const std::string &s) {
    std::cerr << s << std::endl;
    boost::char_separator<char> sep(",");
    boost::tokenizer<boost::char_separator<char> > tok(s, sep);
    boost::tokenizer<boost::char_separator<char> >::iterator i = tok.begin();
    m_seq = boost::lexical_cast<uint32_t>(*i++);
    m_blocks = boost::lexical_cast<uint32_t>(*i++);
    m_bytes = boost::lexical_cast<uint32_t>(*i++);
    m_block_errors = boost::lexical_cast<uint32_t>(*i++);
    m_seq_errors = boost::lexical_cast<uint32_t>(*i++);
    m_send_bytes = boost::lexical_cast<uint32_t>(*i++);
    m_send_blocks = boost::lexical_cast<uint32_t>(*i++);
    m_inject_errors = boost::lexical_cast<uint32_t>(*i++);
    m_queue_size.set(boost::lexical_cast<uint32_t>(*i++));
    m_enc_time.set(boost::lexical_cast<float>(*i++));
    m_send_time.set(boost::lexical_cast<float>(*i++));
    m_pkt_time.set(boost::lexical_cast<float>(*i++));
    m_rssi.set(boost::lexical_cast<int32_t>(*i++),
	       boost::lexical_cast<int32_t>(*i++),
	       boost::lexical_cast<int32_t>(*i));
  }

  std::string serialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::stringstream ss;
    ss << std::setprecision(6)
       << m_seq << ","
       << m_blocks << ","
       << m_bytes << ","
       << m_block_errors << ","
       << m_seq_errors << ","
       << m_send_bytes << ","
       << m_send_blocks << ","
       << m_inject_errors << ","
       << m_queue_size.mean() << ","
       << m_enc_time.mean() << ","
       << m_send_time.mean() << ","
       << m_pkt_time.mean() << ","
       << m_rssi.min() << ","
       << m_rssi.mean() << ","
       << m_rssi.max();
    return ss.str();
  }

private:
  std::string m_name;
  double m_window;
  uint32_t m_seq;
  uint32_t m_blocks;
  uint32_t m_bytes;
  uint32_t m_block_errors;
  uint32_t m_seq_errors;
  uint32_t m_send_bytes;
  uint32_t m_send_blocks;
  uint32_t m_inject_errors;
  uint32_t m_flushes;
  StatsAccumulator<uint32_t> m_queue_size;
  StatsAccumulator<float> m_enc_time;
  StatsAccumulator<float> m_send_time;
  StatsAccumulator<float> m_pkt_time;
  StatsAccumulator<int8_t> m_rssi;
  std::mutex m_mutex;
};

struct WifiOptions {
  WifiOptions(LinkType type = DATA_LINK, uint8_t rate = 18, bool m = false,
	      bool s = false, bool l = false) :
    link_type(type), data_rate(rate), mcs(m), stbc(s), ldpc(l) { }
  LinkType link_type;
  uint8_t data_rate;
  bool mcs;
  bool stbc;
  bool ldpc;
};

struct Message {
  Message() : port(0), priority(0) {}
  Message(size_t max_packet, uint8_t p, uint8_t pri, WifiOptions opt,
	  std::shared_ptr<FECEncoder> e) :
    msg(max_packet), port(p), priority(pri), opts(opt), enc(e) { }
  std::shared_ptr<Message> create(const std::string &s) {
    std::shared_ptr<Message> ret(new Message(s.length(), port, priority, opts, enc));
    std::copy(s.begin(), s.end(), ret->msg.begin());
    return ret;
  }
  std::vector<uint8_t> msg;
  uint8_t port;
  uint8_t priority;
  WifiOptions opts;
  std::shared_ptr<FECEncoder> enc;
};

struct UDPDestination {
  UDPDestination(uint16_t port, const std::string &hostname, std::shared_ptr<FECDecoder> enc) :
    fec(enc), fdout(0) {

    // Initialize the UDP output socket.
    memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = (in_port_t)htons(port);

    // Lookup the IP address from the hostname
    std::string ip;
    if (hostname != "") {
      ip = hostname_to_ip(hostname);
      s.sin_addr.s_addr = inet_addr(ip.c_str());
    } else {
      s.sin_addr.s_addr = INADDR_ANY;
    }
    s.sin_addr.s_addr = inet_addr(ip.c_str());
  }
  UDPDestination(const std::string &filename, std::shared_ptr<FECDecoder> enc) : fec(enc) {
    // Try to open the output file
    fdout = open(filename.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fdout < 0) {
      LOG_CRITICAL << "Error opening an output file: " << filename;
      exit(EXIT_FAILURE);
    }
  }
  struct sockaddr_in s;
  int fdout;
  std::shared_ptr<FECDecoder> fec;
};


/************************************************************************************************
 * Global variables
 ************************************************************************************************/

Logger::LoggerP Logger::g_logger;


/************************************************************************************************
 * Local function definitions
 ************************************************************************************************/


std::string hostname_to_ip(const std::string &hostname) {

  // Try to lookup the host.
  struct hostent *he;
  if ((he = gethostbyname(hostname.c_str())) == NULL) {
    LOG_ERROR << "Error: invalid hostname";
    return "";
  }

  struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
  for(int i = 0; addr_list[i] != NULL; i++) {
    //Return the first one;
    return inet_ntoa(*addr_list[i]);
  }

  return "";
}

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

double cur_time() {
  struct timeval t;
  gettimeofday(&t, NULL);
  return double(t.tv_sec) + double(t.tv_usec) * 1e-6;
}


// Retrieve messages from incoming raw socket queue and send the UDP packets.
void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, uint8_t status_port, TransferStats &stats, 
		   TransferStats &stats_other) {
  double prev_time = cur_time();
  size_t write_errors = 0;
  FECDecoderStats prev_stats;
  while (1) {

    // Ralink and Atheros both always supply the FCS to userspace, no need to check
    //if (prd.m_nRadiotapFlags & IEEE80211_RADIOTAP_F_FCS)
    //bytes -= 4;

    //rx_status->adapter[adapter_no].received_packet_cnt++;
    //	rx_status->adapter[adapter_no].last_update = dbm_ts_now[adapter_no];
    //	fprintf(stderr,"lu[%d]: %lld\n",adapter_no,rx_status->adapter[adapter_no].last_update);
    //	rx_status->adapter[adapter_no].last_update = current_timestamp();

    // Pull the next block off the message queue.
    std::shared_ptr<monitor_message_t> msg = inqueue.pop();
    stats.add_rssi(msg->rssi);

    // Lookup the destination class.
    if (!udp_out[msg->port]) {
      LOG_ERROR << "Error finding the output destination for port " << int(msg->port);
      continue;
    }

    // Add this block to the FEC decoder.
    std::shared_ptr<FECDecoder> fec = udp_out[msg->port]->fec;
    fec->add_block(msg->data.data(), msg->data.size());

    // Output any packets that are finished in the decoder.
    for (std::shared_ptr<FECBlock> block = fec->get_block(); block; block = fec->get_block()) {
      if (block->data_length() > 0) {
	if (udp_out[msg->port]) {
	  if (udp_out[msg->port]->fdout == 0) {
	    sendto(send_sock, block->data(), block->data_length(), 0,
		   (struct sockaddr *)&(udp_out[msg->port]->s), sizeof(struct sockaddr_in));
	  } else if(udp_out[msg->port]->fdout > 0) {
	    if (write(udp_out[msg->port]->fdout, block->data(), block->data_length()) <= 0) {
	      ++write_errors;
	    }
	  }
	}

	// If this is a link status message, parse it and update the stats.
	if (msg->port == status_port) {
	  std::string s(block->data(), block->data() + block->data_length());
	  stats_other.update(s);
	}
      }
    }

    // Accumulate the stats
    stats.add(fec->stats(), prev_stats);
    prev_stats = fec->stats();
  }
}

template <typename tmpl__T>
double mbps(tmpl__T v1, tmpl__T v2, double time) {
  double diff = static_cast<double>(v1) - static_cast<double>(v2);
  return diff * 8e-6 / time;
}

// Send status messages to the other radio and log the FEC stats periodically
void log_thread(TransferStats &stats, TransferStats &stats_other, float syslog_period, float status_period,
		SharedQueue<std::shared_ptr<Message> > &outqueue,
		std::shared_ptr<Message> msg) {

  uint32_t loop_period =
    static_cast<uint32_t>(std::round(1000.0 * ((syslog_period == 0) ? status_period :
					       ((status_period == 0) ? syslog_period :
						std::min(syslog_period, status_period)))));
  transfer_stats_t ps = stats.get_stats();
  double last_stat = cur_time();
  double last_log = cur_time();
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(loop_period));
    double t = cur_time();

    // Send status if it's time
    double stat_dur = t - last_stat;
    if (stat_dur > status_period) {
      std::shared_ptr<Message> omsg = msg->create(stats.serialize());
      outqueue.push(omsg);
    }

    // Post a log message if it's time
    double log_dur = t - last_log;
    if (log_dur >= syslog_period) {
      transfer_stats_t s = stats.get_stats();
      LOG_INFO
	<< std::setprecision(3)
	<< "dur: " << last_log << "  "
	<< "seq: " << (s.sequences - ps.sequences) << "/"
	<< (s.sequence_errors - ps.sequence_errors) << "  "
	<< "blk s,r: " << (s.blocks_out - ps.blocks_out) << "/"
	<< s.inject_errors - ps.inject_errors << " "
	<< (s.blocks_in - ps.blocks_in) << "/"
	<< s.block_errors - ps.block_errors << "  "
	<< "rate s,r: " << mbps(s.bytes_out, ps.bytes_out, log_dur) << "/"
	<< mbps(s.bytes_in, ps.bytes_in, log_dur) << " Mbps"
	<< "  times (e/s/t): " << s.encode_time << "/" << s.send_time << "/"
	<< s.pkt_time << " us"
	<< "  RSSI: " << static_cast<int16_t>(s.rssi)
	<< " (" << static_cast<int16_t>(s.rssi_min) << "/"
	<< static_cast<int16_t>(s.rssi_max) << ")";
      ps = s;
      last_log = t;
      stats.reset_accumulators();
    }
  }
}

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
  float status_period = conf.get<float>("global.statusperiod", 0.1);

  uint16_t max_queue_size = conf.get<uint16_t>("global.maxqueuesize", 200);

  // Create the logger
  Logger::create(log_level, syslog_level, syslog_host);
  LOG_INFO << "wfb_bridge logging '" << log_level << "' to console and '"
	   << syslog_level << "' to syslog";

  // Create the message queues.
  SharedQueue<std::shared_ptr<monitor_message_t> > inqueue;   // Wifi to UDP
  SharedQueue<std::shared_ptr<Message> > outqueue;  // UDP to Wifi

  // Create the the threads for receiving blocks off the UDP sockets
  // and relaying them to the raw socket interface.
  std::vector<std::shared_ptr<std::thread> > thrs;
  TransferStats trans_stats(mode);
  TransferStats trans_stats_other(mode);
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
		      &outqueue, msg]() {
	  log_thread(trans_stats, trans_stats_other, syslog_period, status_period, outqueue, msg);
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
	    msg->msg.resize(count);
	    outqueue.push(msg);
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
  std::vector<std::shared_ptr<UDPDestination> > udp_out(16);
  uint8_t status_port = 0;
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

	// Send message out of the send queue
	while(!terminate) {

	  // Pull the next packet off the queue
	  std::shared_ptr<Message> msg = outqueue.pop();
	  bool flush = (msg->msg.size() == 0);
	  bool debug = !flush && (msg->msg.size() < 50);

	  // FEC encode the packet if requested.
	  double loop_start = cur_time();
	  auto enc = msg->enc;
	  // Flush the encoder if necessary.
	  if (flush) {
	    enc->flush();
	  } else {
	    double enc_start = cur_time();
	    // Get a FEC encoder block
	    std::shared_ptr<FECBlock> block = enc->get_next_block(msg->msg.size());
	    // Copy the data into the block
	    std::copy(msg->msg.data(), msg->msg.data() + msg->msg.size(), block->data());
	    // Pass it off to the FEC encoder.
	    enc->add_block(block);
	    trans_stats.add_encode_time(cur_time() - enc_start);
	  }

	  // Transmit any packets that are finished in the encoder.
	  size_t queue_size = outqueue.size() + enc->n_output_blocks();
	  size_t dropped_blocks;
	  size_t count = 0;
	  size_t nblocks = 0;
	  for (std::shared_ptr<FECBlock> block = enc->get_block(); block;
	       block = enc->get_block()) {
	    double send_start = cur_time();
	    // If the link is slower than the data rate we need to drop some packets.
	    if (block->is_fec_block() &
		((outqueue.size() + enc->n_output_blocks()) > max_queue_size)) {
	      ++dropped_blocks;
	      continue;
	    }
	    raw_send_sock.send(block->pkt_data(), block->pkt_length(), msg->port,
			       msg->opts.link_type, msg->opts.data_rate, msg->opts.mcs,
			       msg->opts.stbc, msg->opts.ldpc);
	    count += block->pkt_length();
	    ++nblocks;
	    trans_stats.add_send_time(cur_time() - send_start);
	  }

	  // Add stats to the accumulator.
	  double cur = cur_time();
	  double loop_time = cur - loop_start;
	  trans_stats.add_send_stats(count, nblocks, dropped_blocks, queue_size, flush, loop_time);
	}
    };
    std::thread send_thread(send_th);

    // Open the raw receive socket
    RawReceiveSocket raw_recv_sock((mode == "ground"));
    bool valid_recv_sock = false;
    for (const auto &device : ifnames) {
      LOG_DEBUG << device;
      if (raw_recv_sock.add_device(device)) {
	valid_recv_sock = true;
	LOG_INFO << "Receiving on interface: " << device;
	break;
      } else {
	LOG_DEBUG << "Error: " << device;
	LOG_DEBUG << "  " << raw_recv_sock.error_msg();
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
