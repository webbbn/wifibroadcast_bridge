
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <wfb_bridge.hh>
#include <udp_send.hh>
#include <logging.hh>

// Retrieve messages from incoming raw socket queue and send the UDP packets.
void fec_decode_thread(MessageQueue &inqueue, PacketQueueP output_queue, TransferStats &stats) {
  double prev_time = cur_time();
  size_t write_errors = 0;
  std::vector<FECDecoder> decoders(RAW_SOCKET_NPORTS);
  std::vector<FECDecoderStats> prev_dec_stats(RAW_SOCKET_NPORTS);

  while (1) {

    // Pull the next block off the message queue.
    std::shared_ptr<monitor_message_t> msg = inqueue.pop();
    if ((msg->data.size() > 0) && (msg->rssi > -100)) {
      stats.add_rssi(msg->rssi);
      stats.add_latency(msg->latency_ms);
    }
    uint8_t port = msg->port;
    if (port >= RAW_SOCKET_NPORTS) {
      continue;
    }

    // Add this block to the FEC decoder.
    FECDecoder &dec = decoders[port];
    dec.add_block(msg->data.data(), msg->data.size());

    // Output any packets that are finished in the decoder.
    for (std::shared_ptr<FECBlock> block = dec.get_block(); block; block = dec.get_block()) {
      if (block->data_length() > 0) {
        Packet pkt = mkpacket(block->data(), block->data() + block->data_length());
        output_queue->push(pkt);
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

void udp_send_loop(PacketQueueP q, const std::string host, uint16_t port) {

  // Try to open a UDP socket.
  int send_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (send_sock < 0) {
    LOG_ERROR << "Error opening the UDP receive socket.";
    return;
  }

  // Set the socket options.
  int optval = 1;
  setsockopt(send_sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
  setsockopt(send_sock, SOL_SOCKET, SO_BROADCAST, (const void *)&optval, sizeof(optval));

  // Initialize the UDP output socket.
  struct sockaddr_in s;
  memset(&s, '\0', sizeof(struct sockaddr_in));
  s.sin_family = AF_INET;
  s.sin_port = (in_port_t)htons(port);
  s.sin_addr.s_addr = inet_addr(host.c_str());

  while (1) {
    Packet msg = q->pop();
    sendto(send_sock, msg->data(), msg->size(), 0,
           (struct sockaddr *)&(s), sizeof(struct sockaddr_in));
  }
}
