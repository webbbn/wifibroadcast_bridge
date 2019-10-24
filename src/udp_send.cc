
#include <sys/stat.h>
#include <fcntl.h>

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

UDPDestination::UDPDestination(uint16_t port, const std::string &hostname,
			       std::shared_ptr<FECDecoder> enc) :
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

UDPDestination::UDPDestination(const std::string &filename, std::shared_ptr<FECDecoder> enc) :
  fec(enc) {
  // Try to open the output file
  fdout = open(filename.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fdout < 0) {
    LOG_CRITICAL << "Error opening an output file: " << filename;
    exit(EXIT_FAILURE);
  }
}


// Retrieve messages from incoming raw socket queue and send the UDP packets.
void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, uint8_t status_port, TransferStats &stats, 
		   TransferStats &stats_other) {
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
    stats.add(fec->stats(), udp_out[msg->port]->prev_stats);
    udp_out[msg->port]->prev_stats = fec->stats();
  }
}
