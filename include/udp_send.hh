
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <wifibroadcast/transfer_stats.hh>
#include <wifibroadcast/raw_socket.hh>
#include <wfb_bridge.hh>
#include <shared_queue.hh>
#include <wifibroadcast/fec.hh>

class UDPDestination {
public:
  UDPDestination(const std::string &outports_str, std::shared_ptr<FECDecoder> enc,
		 bool is_status = false);

  void send(int send_sock, const uint8_t* buf, size_t len);

  void is_status(bool v) {
    m_is_status = v;
  }
  bool is_status() const {
    return m_is_status;
  }

  std::shared_ptr<FECDecoder> fec() {
    return m_fec;
  }

  const FECDecoderStats &prev_stats() const {
    return m_prev_stats;
  }

  void prev_stats(const FECDecoderStats &stats) {
    m_prev_stats = stats;
  }

private:
  std::vector<struct sockaddr_in> m_socks;
  FECDecoderStats m_prev_stats;
  std::shared_ptr<FECDecoder> m_fec;
  bool m_is_status;
};

std::string hostname_to_ip(const std::string &hostname);

void udp_send_loop(SharedQueue<std::shared_ptr<monitor_message_t> > &inqueue,
		   const std::vector<std::shared_ptr<UDPDestination> > &udp_out,
		   int send_sock, TransferStats &stats, TransferStats &stats_other);
