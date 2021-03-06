#ifndef RAW_SOCKET_HH
#define RAW_SOCKET_HH

#include <string>
#include <vector>
#include <chrono>

#include <pcap.h>

#define RAW_SOCKET_OVERHEAD 24
#define RAW_SOCKET_NPORTS 16

enum LinkType {
	       DATA_LINK,
	       FEC_LINK,
	       WFB_LINK,
	       SHORT_DATA_LINK,
	       RTS_DATA_LINK
};

static const uint32_t frequencies_2GHz[] =
  {
   2412,
   2417,
   2422,
   2427,
   2432,
   2437,
   2442,
   2447,
   2452,
   2457,
   2462,
   2467,
   2472,
   2484
  };
static const uint32_t nfreq_2GHz = sizeof(frequencies_2GHz) / sizeof(uint32_t);

static const uint32_t frequencies_5GHz[] =
  {
   5160,
   5170,
   5180,
   5200,
   5220,
   5240,
   5260,
   5280,
   5300,
   5320,
   5500,
   5520,
   5540,
   5560,
   5580,
   5600,
   5620,
   5640,
   5660,
   5680,
   5700,
   5745,
   5765,
   5785,
   5805,
   5825
  };
static const uint32_t nfreq_5GHz = sizeof(frequencies_5GHz) / sizeof(uint32_t);

struct monitor_message_t {
  monitor_message_t(size_t data_size = 0) :
    data(data_size), port(0), link_type(DATA_LINK), rssi(0), rate(0), channel(0),
    channel_flag(0), radiotap_flags(0) {}
  std::vector<uint8_t> data;
  uint8_t port;
  LinkType link_type;
  uint8_t rate;
  uint16_t channel;
  uint16_t channel_flag;
  uint8_t radiotap_flags;
  int8_t rssi;
  uint16_t lock_quality;
  uint16_t latency_ms;
  double recv_time;
  std::vector<uint8_t> antennas;
  std::vector<int8_t> rssis;
};

// Get a list of all the network device names
bool detect_network_devices(std::vector<std::string> &ifnames);

bool set_wifi_up_down(const std::string &device, bool up);
inline bool set_wifi_up(const std::string &device) {
  return set_wifi_up_down(device, true);
}
inline bool set_wifi_down(const std::string &device) {
  return set_wifi_up_down(device, false);
}
bool set_wifi_monitor_mode(const std::string &device);
bool set_wifi_frequency(const std::string &device, uint32_t freq_mhz);
bool set_wifi_txpower_fixed(const std::string &device);
bool set_wifi_txpower(const std::string &device, uint32_t power_mbm);
bool set_wifi_legacy_bitrate(const std::string &device, uint8_t rate);
bool get_wifi_frequency_list(const std::string &device, std::vector<uint32_t> &frequencies);


class RawSendSocket {
public:
  RawSendSocket(bool ground, uint16_t mtu);

  bool error() const {
    return (m_sock < 0);
  }

  bool add_device(const std::string &device, bool silent = true,
                  bool mcs = false, bool stbc = false, bool ldpc = false);

  // Copy the message into the send bufer and send it.
  bool send(const uint8_t *msg, size_t msglen, uint8_t port, LinkType type,
	    uint8_t datarate = 18);
  bool send(const std::vector<uint8_t> &msg, uint8_t port, LinkType type,
	    uint8_t datarate = 18) {
    return send(msg.data(), msg.size(), port, type, datarate);
  }

private:
  bool m_ground;
  bool m_mcs;
  bool m_stbc;
  bool m_ldpc;
  uint16_t m_mtu;
  int m_sock;
  std::vector<uint8_t> m_send_buf;
};

class RawReceiveSocket {
public:
  
  RawReceiveSocket(bool ground, uint16_t mtu);

  bool add_device(const std::string &device);

  bool receive(monitor_message_t &msg, std::chrono::duration<double> timeout) const;

private:
  bool m_ground;
  uint16_t m_mtu;
  pcap_t *m_ppcap;
  int m_selectable_fd;
};

#endif // RAW_SOCKET_HH
