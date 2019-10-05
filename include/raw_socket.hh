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
    data(data_size), seq_num(0), port(0), link_type(0), rssi(0), rate(0), channel(0),
    channel_flag(0), radiotap_flags(0) {}
  std::vector<uint8_t> data;
  uint32_t seq_num;
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

struct RawReceiveStats {
  RawReceiveStats() :
    seq_num(0), prev_good_seq_num(0), cur_error_count(0), resets(0), good_packets(0),
    dropped_packets(0), error_packets(0), packets(0), bytes(0) { }
  uint32_t seq_num;
  uint32_t prev_good_seq_num;
  uint32_t cur_error_count;
  uint32_t resets;
  uint32_t good_packets;
  uint32_t dropped_packets;
  uint32_t error_packets;
  uint32_t packets;
  uint64_t bytes;
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

  uint8_t *send_buffer();

  // Send a message from the internal message buffer.
  bool send(size_t msglen, uint8_t port, LinkType type, uint8_t datarate = 18,
	    bool mcs = false, bool stbc = false, bool ldpc = false);

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
  uint8_t m_hdr_len;
  uint32_t m_seq_num;
};

class RawReceiveSocket {
public:
  RawReceiveSocket(bool ground, uint32_t max_packet = 65535);

  bool add_device(const std::string &device);

  bool receive(monitor_message_t &msg);

  const RawReceiveStats &stats() const {
    return m_stats;
  }

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
  RawReceiveStats m_stats;
};

#endif // RAW_SOCKET_HH
