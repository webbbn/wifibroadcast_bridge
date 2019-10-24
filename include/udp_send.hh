
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <transfer_stats.hh>
#include <raw_socket.hh>
#include <wfb_bridge.hh>
#include <shared_queue.hh>
#include <fec.hh>

struct UDPDestination {
  UDPDestination(uint16_t port, const std::string &hostname, std::shared_ptr<FECDecoder> enc);
  UDPDestination(const std::string &filename, std::shared_ptr<FECDecoder> enc);

  struct sockaddr_in s;
  int fdout;
  FECDecoderStats prev_stats;
  std::shared_ptr<FECDecoder> fec;
};

std::string hostname_to_ip(const std::string &hostname);

void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, uint8_t status_port, TransferStats &stats, 
		   TransferStats &stats_other);
