
#include <algorithm>
#include <thread>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <linux/wireless.h>
#include <ifaddrs.h>

#include <iostream>

#include <raw_socket.hh>
#include <logging.hh>
#include <pcap-bpf.h>
#include <radiotap.h>
#include <radiotap_iter.h>

#define PCAP_PACKET_CAPTURE_SIZE 65536 // Maximum receive message size
#define PCAP_PACKET_BUFFER_TIMEOUT_MS 1 // Don't wait for multiple messages

#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x01
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x02
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04
#define IEEE80211_RADIOTAP_MCS_HAVE_FMT   0x08

#define IEEE80211_RADIOTAP_MCS_BW_20    0
#define IEEE80211_RADIOTAP_MCS_BW_40    1
#define IEEE80211_RADIOTAP_MCS_BW_20L   2
#define IEEE80211_RADIOTAP_MCS_BW_20U   3
#define IEEE80211_RADIOTAP_MCS_SGI      0x04
#define IEEE80211_RADIOTAP_MCS_FMT_GF   0x08
#define IEEE80211_RADIOTAP_MCS_HAVE_FEC   0x10
#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20

#define IEEE80211_RADIOTAP_MCS_FEC_LDPC   0x10
#define	IEEE80211_RADIOTAP_MCS_STBC_MASK  0x60
#define	IEEE80211_RADIOTAP_MCS_STBC_0  0x00
#define	IEEE80211_RADIOTAP_MCS_STBC_1  0x20
#define	IEEE80211_RADIOTAP_MCS_STBC_2  0x40
#define	IEEE80211_RADIOTAP_MCS_STBC_3  0x60
#define	IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

#define RADIOTAP_RATE_PRESENT_FLAG (1 << 2)
#define RADIOTAP_TX_PRESENT_FLAG (1 << 15)
#define RADIOTAP_MCS_PRESENT_FLAG (1 << 19)

#define RADIOTAP_TX_FLAG_NO_ACK 0x0008

typedef struct {
  pcap_t *ppcap;
  int selectable_fd;
  int n80211HeaderLength;
} monitor_interface_t;

#pragma pack()
struct radiotap_header_legacy {
  radiotap_header_legacy() :
    version(0), pad1(0), len(12), present(RADIOTAP_RATE_PRESENT_FLAG | RADIOTAP_TX_PRESENT_FLAG),
    datarate(0x16), tx_flags(RADIOTAP_TX_FLAG_NO_ACK) {}
  uint8_t version;
  uint8_t pad1;
  uint16_t len;
  uint32_t present;
  uint8_t datarate;
  uint8_t pad2;
  uint16_t tx_flags;
} __attribute__((__packed__));

#pragma pack()
struct radiotap_header_mcs {
  radiotap_header_mcs() :
    version(0), pad1(0), len(13), present(RADIOTAP_TX_PRESENT_FLAG | RADIOTAP_MCS_PRESENT_FLAG),
    tx_flags(RADIOTAP_TX_FLAG_NO_ACK), mcs_known(0), mcs_flags(0), mcs_rate(0) {}
  uint8_t version;
  uint8_t pad1;
  uint16_t len;
  uint32_t present;
  uint16_t tx_flags;
  uint8_t mcs_known;
  uint8_t mcs_flags;
  uint8_t mcs_rate;
} __attribute__((__packed__));

#if 0
static uint8_t radiotap_header[] = {
  0x00, 0x00, // Radiotap version
  0x0b, 0x00, // Radiotap header length
  0x04, 0x80, 0x00, 0x00, // Radiotap present flags (rate (bit 2) + tx flags (bit 15))
  0x16, // Datarate
  0x08, 0x00 // RADIOTAP_F_TX_NOACK
};

static uint8_t radiotap_header_mcs[] = {
  0x00, 0x00, // Radiotap version (0)
  0x0d, 0x00, // Radiotap header length
  0x00, 0x80, 0x08, 0x00, // Radiotap present flags (tx flags (bit 15) + mcs flags (bit 19))
  0x08, 0x00, // RADIOTAP_F_TX_NOACK
  0x00, 0x00, 0x00 // MCS: bitmap, flags, mcs_index
};
#endif

static uint8_t u8aIeeeHeader_data_short[] = {
  0x08, 0x01, 0x00, 0x00, // frame control field (2bytes), duration (2 bytes)
  0xff // port =  1st byte of IEEE802.11 RA (mac) must be something odd
  // (wifi hardware determines broadcast/multicast through odd/even check)
};

static uint8_t ieee_header_data[] = {
  0x08, 0x02, 0x00, 0x00, // frame control field (2bytes), duration (2 bytes)
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // port = 1st byte of IEEE802.11 RA (mac) must be something
  // odd (wifi hardware determines broadcast/multicast through odd/even check)
  0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // receiver mac address
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // transmitter mac address
  0x00, 0x00 // IEEE802.11 seqnum, (will be overwritten later by Atheros firmware/wifi chip)
};

static uint8_t u8aIeeeHeader_rts[] = {
  0xb4, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
  0xff, //  port = 1st byte of IEEE802.11 RA (mac) must be something odd
  // (wifi hardware determines broadcast/multicast through odd/even check)
};


bool detect_network_devices(std::vector<std::string> &ifnames) {
  ifnames.clear();

  // Get the wifi interfaces.
  struct ifaddrs *ifaddr;
  if (getifaddrs(&ifaddr) == -1) {
    return false;
  }

  // Create the list of interface names.
  struct ifaddrs *ifa;
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }
    // Only return AF_PACKET interfaces
    if (ifa->ifa_addr->sa_family == AF_PACKET) {
      ifnames.push_back(ifa->ifa_name);
    }
  }

  freeifaddrs(ifaddr);
  return true;
}

inline uint32_t cur_microseconds() {
  struct timeval t;
  gettimeofday(&t, 0);
  return t.tv_usec;
}

inline uint16_t cur_milliseconds() {
  uint32_t us = cur_microseconds();
  return static_cast<uint16_t>(std::round(us / 1000.0));
}


/******************************************************************************
 * RawSendSocket
 *****************************************************************************/

RawSendSocket::RawSendSocket(bool ground, uint32_t send_buffer_size, uint32_t max_packet) :
  m_ground(ground), m_max_packet(max_packet) {
  size_t max_header = std::max(sizeof(radiotap_header_legacy), sizeof(radiotap_header_mcs));

  // Create the send buffer with the appropriate headers.
  m_send_buf.resize(max_header + sizeof(ieee_header_data) + max_packet);
}

bool RawSendSocket::add_device(const std::string &device, bool silent) {

  m_sock = socket(AF_PACKET, SOCK_RAW, 0);
  if (m_sock == -1) {
    if (!silent) {
      LOG_ERROR << "Socket open failed.";
    }
    return false;
  }

  struct sockaddr_ll ll_addr;
  ll_addr.sll_family = AF_PACKET;
  ll_addr.sll_protocol = 0;
  ll_addr.sll_halen = ETH_ALEN;

  struct ifreq ifr;
  strncpy(ifr.ifr_name, device.c_str(), IFNAMSIZ);

  // Get the current mode.
  struct iwreq mode;
  memset(&mode, 0, sizeof(mode));
  strncpy(mode.ifr_name, device.c_str(), device.length());
  mode.ifr_name[device.length()] = 0;
  if ((ioctl(m_sock, SIOCGIWMODE, &mode) < 0) || (mode.u.mode != IW_MODE_MONITOR)) {
    return false;
  }

  if (ioctl(m_sock, SIOCGIFINDEX, &ifr) < 0) {
    if (!silent) {
      LOG_ERROR << "Error: ioctl(SIOCGIFINDEX) failed.";
    }
    close(m_sock);
    m_sock = -1;
    return false;
  }
  ll_addr.sll_ifindex = ifr.ifr_ifindex;

  if (ioctl(m_sock, SIOCGIFHWADDR, &ifr) < 0) {
    if (!silent) {
      LOG_ERROR << "Error: ioctl(SIOCGIFHWADDR) failed.";
    }
    close(m_sock);
    m_sock = -1;
    return false;
  }
  memcpy(ll_addr.sll_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

  if (bind(m_sock, (struct sockaddr *)&ll_addr, sizeof(ll_addr)) == -1) {
    if (!silent) {
      LOG_ERROR << "Error: bind failed.";
    }
    close(m_sock);
    m_sock = -1;
    return false;
  }

  if (m_sock == -1) {
    if (!silent) {
      LOG_ERROR << "Error: Cannot open socket: Must be root with an 802.11 card with RFMON enabled";
    }
    return false;
  }

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 8000;
  if (setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
    if (!silent) {
      LOG_ERROR << "setsockopt SO_SNDTIMEO";
    }
    return false;
  }

  if (setsockopt(m_sock, SOL_SOCKET, SO_SNDBUF, &m_buffer_size, sizeof(m_buffer_size)) < 0) {
    if (!silent) {
      LOG_ERROR << "setsockopt SO_SNDBUF";
    }
    return false;
  }

  return true;
}

bool RawSendSocket::send(const uint8_t *msg, size_t msglen, uint8_t port, LinkType type,
			 uint8_t datarate, bool mcs, bool stbc, bool ldpc) {

  // Construct the radiotap header at the nead of the packet.
  size_t rt_hlen = 0;
  if (mcs) {
    rt_hlen = sizeof(radiotap_header_mcs);
    radiotap_header_mcs head;
    head.mcs_known = (IEEE80211_RADIOTAP_MCS_HAVE_MCS |
		      IEEE80211_RADIOTAP_MCS_HAVE_BW |
		      IEEE80211_RADIOTAP_MCS_HAVE_GI |
		      IEEE80211_RADIOTAP_MCS_HAVE_STBC |
		      IEEE80211_RADIOTAP_MCS_HAVE_FEC);
    head.mcs_flags = 0;
    if(stbc) {
      head.mcs_flags |= IEEE80211_RADIOTAP_MCS_STBC_1;
    }
    if(ldpc) {
      head.mcs_flags |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
    }
    head.mcs_rate = datarate;
    memcpy(m_send_buf.data(), reinterpret_cast<uint8_t*>(&head), rt_hlen);
  } else {
    rt_hlen = sizeof(radiotap_header_legacy);
    radiotap_header_legacy head;

    // Set the data rate in the header
    switch (datarate) {
    case 1:
      head.datarate = 2;
      break;
    case 2:
      head.datarate = 4;
      break;
    case 3:
      head.datarate = 11;
      break;
    case 4:
      head.datarate = 22;
      break;
    case 5:
      head.datarate = 12;
      break;
    case 6:
      head.datarate = 18;
      break;
    case 7:
      head.datarate = 24;
      break;
    case 8:
      head.datarate = 36;
      break;
    case 9:
      head.datarate = 48;
      break;
    case 10:
      head.datarate = 72;
      break;
    case 11:
      head.datarate = 96;
      break;
    case 12:
      head.datarate = 108;
      break;
    default:
      head.datarate = 22;
      break;
    }
    memcpy(m_send_buf.data(), reinterpret_cast<uint8_t*>(&head), rt_hlen);
  }

  // Copy the 802.11 header onto the head of the packet.
  size_t ieee_hlen = sizeof(ieee_header_data);
  memcpy(m_send_buf.data() + rt_hlen, ieee_header_data, ieee_hlen);

  // Set the port in the header
  m_send_buf[rt_hlen + 4] = (((port & 0xf) << 4) | (m_ground ? 0xd : 0x5));

  // Add the timestamp to the header.
  uint8_t send_time = static_cast<uint8_t>(cur_milliseconds() % 256);
  m_send_buf[rt_hlen + 5] = send_time;

  // Copy the data into the buffer.
  memcpy(m_send_buf.data() + rt_hlen + ieee_hlen, msg, msglen);

  // Send the packet
  return (::send(m_sock, m_send_buf.data(), rt_hlen + ieee_hlen + msglen, 0) >= 0);
}

/******************************************************************************
 * RawReceiveSocket
 *****************************************************************************/

RawReceiveSocket::RawReceiveSocket(bool ground, uint32_t max_packet) :
  m_ground(ground), m_max_packet(max_packet) {
}

bool RawReceiveSocket::add_device(const std::string &device) {

  // open the interface in pcap
  char errbuf[PCAP_ERRBUF_SIZE];
  errbuf[0] = '\0';
  m_ppcap = pcap_open_live(device.c_str(), PCAP_PACKET_CAPTURE_SIZE, 0,
			   PCAP_PACKET_BUFFER_TIMEOUT_MS, errbuf);
  if (m_ppcap == NULL) {
    LOG_ERROR << "Unable to open " + device + ": " + std::string(errbuf);
    return false;
  }

  if (pcap_setdirection(m_ppcap, PCAP_D_IN) < 0) {
    LOG_ERROR << "Error setting " + device + " direction";
    return false;
  }

  // Set non-blocking mode so that we can control timeouts
  if (pcap_setnonblock(m_ppcap, 1, errbuf) == PCAP_ERROR) {
    LOG_ERROR << "Unable to set non-blocking mode on raw send socket";
    LOG_ERROR << errbuf;
    return false;
  }

  int nLinkEncap = pcap_datalink(m_ppcap);
  if (nLinkEncap != DLT_IEEE802_11_RADIO) {
    LOG_DEBUG << "Unknown encapsulation on " + device +
      "! check if monitor mode is supported and enabled";
    return false;
  }

  // Match the first 4 bytes of the destination address.
  struct bpf_program bpfprogram;
  const char *filter_gnd = "(ether[0x00:2] == 0x0801 || ether[0x00:2] == 0x0802 || ether[0x00:4] == 0xb4010000) && ((ether[0x04:1] & 0x0f) == 0x05)";
  const char *filter_air = "(ether[0x00:2] == 0x0801 || ether[0x00:2] == 0x0802 || ether[0x00:4] == 0xb4010000) && ((ether[0x04:1] & 0x0f) == 0x0d)";
  const char *filter = (m_ground ? filter_gnd : filter_air);
  if (pcap_compile(m_ppcap, &bpfprogram, filter, 1, 0) == -1) {
    LOG_ERROR << "Error compiling bpf program: " + std::string(filter);
    return false;
  }

  // Configure the filter.
  if (pcap_setfilter(m_ppcap, &bpfprogram) == -1) {
    LOG_ERROR << "Error configuring the bpf program: " + std::string(filter);
    return false;
  }
  pcap_freecode(&bpfprogram);

  m_selectable_fd = pcap_get_selectable_fd(m_ppcap);

  return true;
}

bool RawReceiveSocket::receive(monitor_message_t &msg, std::chrono::duration<double> timeout) {
  struct pcap_pkthdr *pcap_packet_header = NULL;
  uint8_t const *pcap_packet_data = NULL;

  auto start = std::chrono::high_resolution_clock::now();
  int retval;
  while((retval = pcap_next_ex(m_ppcap, &pcap_packet_header, &pcap_packet_data)) == 0) {
    auto cur = std::chrono::high_resolution_clock::now();
    if ((cur - start) > timeout) {
      // An empty message with a return of true implies timeout
      msg.data.clear();
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (retval < 0) {
    LOG_ERROR << "Error receiving from the raw data socket.";
    LOG_ERROR << "  " << std::string(pcap_geterr(m_ppcap));
    return false;
  }

  // fetch radiotap header length from radiotap header (seems to be 36 for Atheros and 18 for Ralink)
  uint16_t rt_header_len = (pcap_packet_data[3] << 8) + pcap_packet_data[2];

  // check for packet type and set headerlen accordingly
  pcap_packet_data += rt_header_len;
  switch (pcap_packet_data[1]) {
  case 0x01: // data short, rts
    m_n80211HeaderLength = 0x05;
    break;
  case 0x02: // data
    m_n80211HeaderLength = 0x18;
    break;
  default:
    break;
  }

  // Extract the port out of the ieee header
  msg.port = (pcap_packet_data[4] >> 4);

  // Extract the time stamp out of the header.
  uint32_t send_time = pcap_packet_data[5];
  uint32_t cur_time = cur_milliseconds();
  msg.latency_ms = (send_time < cur_time) ? (cur_time - send_time) :
    (cur_time + 256 - send_time);

  // Skip past the radiotap header.
  pcap_packet_data -= rt_header_len;

  if (pcap_packet_header->len < static_cast<uint32_t>(rt_header_len + m_n80211HeaderLength)) {
    LOG_ERROR << "rx ERROR: ppcapheaderlen < u16headerlen + n80211headerlen";
    return false;
  }

  struct ieee80211_radiotap_iterator rti;
  if (ieee80211_radiotap_iterator_init(&rti,(struct ieee80211_radiotap_header *)pcap_packet_data,
				       pcap_packet_header->len, NULL) < 0) {
    LOG_ERROR << "rx ERROR: radiotap_iterator_init < 0";
    return false;
  }

  msg.rssis.clear();
  uint8_t antenna = 0;
  int n;
  while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0) {
    switch (rti.this_arg_index) {
    case IEEE80211_RADIOTAP_RATE:
      msg.rate = (*rti.this_arg);
      break;
    case IEEE80211_RADIOTAP_CHANNEL:
      msg.channel = *((uint16_t *)rti.this_arg);
      msg.channel_flag = *((uint16_t *)(rti.this_arg + 2));
      break;
    case IEEE80211_RADIOTAP_ANTENNA:
      antenna = *reinterpret_cast<uint8_t*>(rti.this_arg);
      break;
    case IEEE80211_RADIOTAP_FLAGS:
      msg.radiotap_flags = *rti.this_arg;
      break;
    case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
      while (msg.rssis.size() <= antenna) {
	msg.rssis.push_back(-100);
      }
      msg.rssis[antenna] = *reinterpret_cast<int8_t*>(rti.this_arg);
      break;
    case IEEE80211_RADIOTAP_LOCK_QUALITY:
      msg.lock_quality = *((uint16_t *)rti.this_arg);
      break;
    }
  }

  // Determine the best RSSI value
  if (msg.rssis.empty()) {
    msg.rssi = -100;
  } else {
    msg.rssi = *std::max_element(msg.rssis.begin(), msg.rssis.end());
  }

  // Copy the data into the message buffer.
  const uint32_t crc_len = 4;
  uint32_t header_len = rt_header_len + m_n80211HeaderLength;
  uint32_t packet_len = pcap_packet_header->len - header_len - crc_len;
  msg.data.resize(packet_len);
  std::copy(pcap_packet_data + header_len, pcap_packet_data + header_len + packet_len,
	    msg.data.begin());

  return true;
}
