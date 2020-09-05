
#pragma once

#include <sys/time.h>

#include <wifibroadcast/fec.hh>
#include <wifibroadcast/raw_socket.hh>
#include <shared_queue.hh>

#define MAX_PORTS 64

inline double cur_time() {
  struct timeval t;
  gettimeofday(&t, 0);
  return double(t.tv_sec) + double(t.tv_usec) * 1e-6;
}

typedef SharedQueue<std::shared_ptr<struct monitor_message_t> > MessageQueue;
typedef std::shared_ptr<std::vector<uint8_t> > Packet;
typedef SharedQueue<Packet> PacketQueue;
inline Packet mkpacket(size_t size) {
  return Packet(new std::vector<uint8_t>(size));
}
template <typename tmpl__Itr>
inline Packet mkpacket(tmpl__Itr begin, tmpl__Itr end) {
  return Packet(new std::vector<uint8_t>(begin, end));
}
