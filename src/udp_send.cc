
#include <sys/stat.h>
#include <fcntl.h>

#include <boost/lexical_cast.hpp>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <wfb_bridge.hh>
#include <udp_send.hh>
#include <logging.hh>

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

UDPDestination::UDPDestination(const std::string &outports_str, std::shared_ptr<FECDecoder> enc,
			       bool is_status) :
  m_fec(enc), m_is_status(is_status) {

  // Get the remote hostname/ip(s) and port(s)
  std::vector<std::string> outports;
  boost::algorithm::split(outports, outports_str, boost::is_any_of(","));
  m_socks.resize(outports.size());

  // Split out the hostnames and ports
  for (size_t i = 0; i < outports.size(); ++i) {
    std::vector<std::string> host_port;
    boost::algorithm::split(host_port, outports[i], boost::is_any_of(":"));
    if (host_port.size() != 2) {
      LOG_CRITICAL << "Invalid host:port specified (" << outports[i] << ")";
      return;
    }

    // Initialize the UDP output socket.
    const std::string &hostname = host_port[0];
    struct sockaddr_in &s = m_socks[i];
    uint16_t port = boost::lexical_cast<uint16_t>(host_port[1]);
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
}

void UDPDestination::send(int send_sock, const uint8_t* buf, size_t len) {
  for (const auto &s : m_socks) {
    sendto(send_sock, buf, len, 0, (struct sockaddr *)&(s), sizeof(struct sockaddr_in));
  }
}

// Retrieve messages from incoming raw socket queue and send the UDP packets.
void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, TransferStats &stats, TransferStats &stats_other) {
  double prev_time = cur_time();
  size_t write_errors = 0;
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
    stats.add_latency(msg->latency_ms);

    // Lookup the destination class.
    if (!udp_out[msg->port]) {
      LOG_ERROR << "Error finding the output destination for port " << int(msg->port);
      continue;
    }

    // Add this block to the FEC decoder.
    std::shared_ptr<FECDecoder> fec = udp_out[msg->port]->fec();
    fec->add_block(msg->data.data(), msg->data.size());

    // Output any packets that are finished in the decoder.
    for (std::shared_ptr<FECBlock> block = fec->get_block(); block; block = fec->get_block()) {
      if (block->data_length() > 0) {
	if (udp_out[msg->port]) {
	  udp_out[msg->port]->send(send_sock, block->data(), block->data_length());
	}

	// If this is a link status message, parse it and update the stats.
	if (udp_out[msg->port]->is_status()) {
	  std::string s(block->data(), block->data() + block->data_length());
	  stats_other.update(s);
	}
      }
    }

    // Accumulate the stats
    stats.add(fec->stats(), udp_out[msg->port]->prev_stats());
    udp_out[msg->port]->prev_stats(fec->stats());
  }
}
