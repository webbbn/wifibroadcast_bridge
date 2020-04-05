#ifndef RAW_SOCKET_HH
#define RAW_SOCKET_HH

#include <string>
#include <vector>

#include <pcap.h>

enum LinkType {
	       DATA_LINK,
	       FEC_LINK,
	       WFB_LINK,
	       SHORT_DATA_LINK,
	       RTS_DATA_LINK
};

struct monitor_message_t {
  monitor_message_t(size_t data_size = 0) :
    data(data_size), port(0), link_type(0), rssi(0), rate(0), channel(0),
    channel_flag(0), radiotap_flags(0) {}
  std::vector<uint8_t> data;
  uint8_t port;
  uint8_t link_type;
  uint8_t rate;
  uint16_t channel;
  uint16_t channel_flag;
  uint8_t radiotap_flags;
  int8_t rssi;
  uint16_t lock_quality;
  uint16_t latency_ms;
  std::vector<uint8_t> antennas;
  std::vector<int8_t> rssis;
};

// Get a list of all the network device names
bool detect_network_devices(std::vector<std::string> &ifnames);

class RawSendSocket {
public:
  RawSendSocket(bool ground, uint32_t buffer_size = 131072, uint32_t max_packet = 65535);

  bool error() const {
    return (m_sock < 0);
  }

  bool add_device(const std::string &device, bool silent = true);

  // Copy the message into the send bufer and send it.
  bool send(const uint8_t *msg, size_t msglen, uint8_t port, LinkType type,
	    uint8_t datarate = 18, bool mcs = false, bool stbc = false, bool ldpc = false);
  bool send(const std::vector<uint8_t> &msg, uint8_t port, LinkType type,
	    uint8_t datarate = 18, bool mcs = false, bool stbc = false, bool ldpc = false) {
    return send(msg.data(), msg.size(), port, type, datarate, mcs, stbc, ldpc);
  }

private:
  bool m_ground;
  uint32_t m_max_packet;
  uint32_t m_buffer_size;
  int m_sock;
  std::vector<uint8_t> m_send_buf;
};

class RawReceiveSocket {
public:
  
  RawReceiveSocket(bool ground, uint32_t max_packet = 65535);

  bool add_device(const std::string &device);

  bool receive(monitor_message_t &msg, std::chrono::duration<double> timeout);

private:
  bool m_ground;
  uint32_t m_max_packet;
  pcap_t *m_ppcap;
  int m_selectable_fd;
  int m_n80211HeaderLength;
};

#endif // RAW_SOCKET_HH
