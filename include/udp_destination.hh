
#pragma once

#include <string>
#include <memory>
#include <vector>

#include <wifibroadcast/fec.hh>

class UDPDestination {
public:
  UDPDestination(const std::string &outports_str,
                 std::shared_ptr<FECDecoder> enc,
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
