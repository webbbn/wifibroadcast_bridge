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
  std::vector<uint8_t> antennas;
  std::vector<int8_t> rssis;
};

struct transfer_stats_t {
  transfer_stats_t(uint32_t _sequences = 0, uint32_t _blocks_in = 0, uint32_t _blocks_out = 0,
		   uint32_t _bytes_in = 0, uint32_t _bytes_out = 0, uint32_t _block_errors = 0,
		   uint32_t _sequence_errors = 0, uint32_t _inject_errors = 0,
		   float _encode_time = 0, float _send_time = 0, float _pkt_time = 0,
		   int8_t _rssi= 0, int8_t _rssi_min = 0, int8_t _rssi_max = 0) :
    sequences(_sequences), blocks_in(_blocks_in), blocks_out(_blocks_out),
    sequence_errors(_sequence_errors), block_errors(_block_errors), inject_errors(_inject_errors),
    bytes_in(_bytes_in), bytes_out(_bytes_out),
    encode_time(_encode_time), send_time(_send_time), pkt_time(_pkt_time),
    rssi(_rssi), rssi_min(_rssi_min), rssi_max(_rssi_max) {}
  uint32_t sequences;
  uint32_t blocks_in;
  uint32_t blocks_out;
  uint32_t sequence_errors;
  uint32_t block_errors;
  uint32_t inject_errors;
  uint32_t bytes_in;
  uint32_t bytes_out;
  float encode_time;
  float send_time;
  float pkt_time;
  int8_t rssi;
  int8_t rssi_min;
  int8_t rssi_max;
};

// Get a list of all the network device names
bool detect_network_devices(std::vector<std::string> &ifnames);

class RawSendSocket {
public:
  RawSendSocket(bool ground, uint32_t buffer_size = 131072, uint32_t max_packet = 65535);

  bool error() const {
    return (m_sock < 0);
  }

  bool add_device(const std::string &device);

  // Copy the message into the send bufer and send it.
  bool send(const uint8_t *msg, size_t msglen, uint8_t port, LinkType type,
	    uint8_t datarate = 18, bool mcs = false, bool stbc = false, bool ldpc = false);
  bool send(const std::vector<uint8_t> &msg, uint8_t port, LinkType type,
	    uint8_t datarate = 18, bool mcs = false, bool stbc = false, bool ldpc = false) {
    return send(msg.data(), msg.size(), port, type, datarate, mcs, stbc, ldpc);
  }

  const std::string &error_msg() const {
    return m_error_msg;
  }

private:
  bool m_ground;
  uint32_t m_max_packet;
  uint32_t m_buffer_size;
  int m_sock;
  std::string m_error_msg;
  std::vector<uint8_t> m_send_buf;
};

class RawReceiveSocket {
public:
  RawReceiveSocket(bool ground, uint32_t max_packet = 65535);

  bool add_device(const std::string &device);

  bool receive(monitor_message_t &msg);

  const std::string &error_msg() const {
    return m_error_msg;
  }

private:
  bool m_ground;
  uint32_t m_max_packet;
  pcap_t *m_ppcap;
  int m_selectable_fd;
  int m_n80211HeaderLength;
  std::string m_error_msg;
};

#endif // RAW_SOCKET_HH
