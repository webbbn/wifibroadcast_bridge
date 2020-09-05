
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <wfb_bridge.hh>
#include <udp_send.hh>
#include <logging.hh>

// Retrieve messages from incoming raw socket queue and send the UDP packets.
void fec_decode_thread(MessageQueue &inqueue, std::vector<std::vector<PacketQueue> > &output_queues,
                       TransferStats &stats, TransferStats &stats_other, uint8_t stats_port) {
  double prev_time = cur_time();
  size_t write_errors = 0;
  std::vector<FECDecoder> decoders(MAX_PORTS);
  std::vector<FECDecoderStats> prev_dec_stats(MAX_PORTS);

  while (1) {

    // Pull the next block off the message queue.
    std::shared_ptr<monitor_message_t> msg = inqueue.pop();
    stats.add_rssi(msg->rssi);
    stats.add_latency(msg->latency_ms);
    uint8_t port = msg->port;
    if (port >= MAX_PORTS) {
      continue;
    }

    // Add this block to the FEC decoder.
    FECDecoder &dec = decoders[port];
    dec.add_block(msg->data.data(), msg->data.size());

    // Output any packets that are finished in the decoder.
    for (std::shared_ptr<FECBlock> block = dec.get_block(); block; block = dec.get_block()) {
      if (block->data_length() > 0) {
        Packet pkt = mkpacket(block->data(), block->data() + block->data_length());
        for (auto &q : output_queues[port]) {
          q.push(pkt);
        }

	// If this is a link status message, parse it and update the stats.
	if (port == stats_port) {
	  std::string s(block->data(), block->data() + block->data_length());
	  stats_other.update(s);
	}
      }
    }

    // Accumulate the decoder stats
    stats.add(dec.stats(), prev_dec_stats[port]);
    prev_dec_stats[port] = dec.stats();
  }
}

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

void udp_send_loop(PacketQueue &q, const std::string host, uint16_t port) {

  // Open the socket
  int send_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (send_sock < 0) {
    LOG_CRITICAL << "Error opening the UDP send socket.";
    return;
  }
  int trueflag = 1;
  if (setsockopt(send_sock, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof(trueflag)) < 0) {
    LOG_CRITICAL << "Error setting the UDP send socket to broadcast.";
    return;
  }

  // Initialize the UDP output socket.
  struct sockaddr_in s;
  memset(&s, '\0', sizeof(struct sockaddr_in));
  s.sin_family = AF_INET;
  s.sin_port = (in_port_t)htons(port);

  // Lookup the IP address from the hostname
  std::string ip;
  if (host != "") {
    ip = hostname_to_ip(host);
    s.sin_addr.s_addr = inet_addr(ip.c_str());
  } else {
    s.sin_addr.s_addr = INADDR_ANY;
  }
  s.sin_addr.s_addr = inet_addr(ip.c_str());

  while (1) {
    Packet msg = q.pop();
    sendto(send_sock, msg->data(), msg->size(), 0,
           (struct sockaddr *)&(s), sizeof(struct sockaddr_in));
  }
}
