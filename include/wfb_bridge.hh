
#pragma once

#include <sys/time.h>
#include <sys/stat.h>

#include <sstream>

#include <INIReader.h>

#include <wifibroadcast/fec.hh>
#include <wifibroadcast/raw_socket.hh>
#include <shared_queue.hh>
#include <logging.hh>

inline double cur_time() {
  struct timeval t;
  gettimeofday(&t, 0);
  return double(t.tv_sec) + double(t.tv_usec) * 1e-6;
}

typedef SharedQueue<std::shared_ptr<struct monitor_message_t> > MessageQueue;
typedef std::shared_ptr<std::vector<uint8_t> > Packet;
typedef SharedQueue<Packet>  PacketQueue;
typedef std::shared_ptr<PacketQueue> PacketQueueP;
typedef std::vector<PacketQueueP> PacketQueues;
inline Packet mkpacket(size_t size) {
  return Packet(new std::vector<uint8_t>(size));
}
inline Packet mkpacket(const std::vector<uint8_t> &buf) {
  return Packet(new std::vector<uint8_t>(buf));
}
template <typename tmpl__Itr>
inline Packet mkpacket(tmpl__Itr begin, tmpl__Itr end) {
  return Packet(new std::vector<uint8_t>(begin, end));
}

struct WifiOptions {
  WifiOptions(LinkType type = DATA_LINK, uint8_t rate = 18) :
    link_type(type), data_rate(rate) { }
  LinkType link_type;
  uint8_t data_rate;
};

struct Message {
  Message() : port(0), ip_port(0), priority(0) {}
  Message(size_t max_packet, uint8_t p, uint16_t ip, uint8_t pri, WifiOptions opt,
	  std::shared_ptr<FECEncoder> e) :
    msg(max_packet), port(p), ip_port(ip), priority(pri), opts(opt), enc(e) { }
  std::shared_ptr<Message> create(const std::string &s, uint16_t ip) {
    std::shared_ptr<Message> ret(new Message(s.length(), port, ip, priority, opts, enc));
    std::copy(s.begin(), s.end(), ret->msg.begin());
    return ret;
  }
  std::shared_ptr<Message> copy(const std::vector<uint8_t> &data, uint16_t ip) const {
    std::shared_ptr<Message> ret(new Message(data.size(), port, ip, priority, opts, enc));
    std::copy(data.begin(), data.end(), ret->msg.begin());
    return ret;
  }
  std::vector<uint8_t> msg;
  uint8_t port;
  uint16_t ip_port;
  uint8_t priority;
  WifiOptions opts;
  std::shared_ptr<FECEncoder> enc;
};

static void splitstr(const std::string& str, std::vector<std::string> &tokens, char delim) {
  std::istringstream iss(str);
  std::string token;
  while (std::getline(iss, token, delim)) {
    tokens.push_back(token);
  }
}

static std::string datetime() {
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];

  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,80,"%Y-%m-%d:%H-%M-%S", timeinfo);
  return std::string(buffer);
}

static bool mkpath(std::string path) {
  // Try to create the directory
  if (::mkdir(path.c_str(), 0775) == 0) {
    // Success!
    return true;
  }
  switch(errno) {
  case EEXIST:
    // Directory already exists.
    return true;
  case ENOENT:
    // Parent didn't exist, try to create it
    if (mkpath(path.substr(0, path.find_last_of('/')))) {
      // Now, try to create again.
      return (0 == ::mkdir(path.c_str(), 0775));
    }
    break;
  default:
    // Some other error, just fail
    break;
  }
  return false;
}

static bool parse_portstr(const INIReader &conf,
                          const std::string &group,
                          const std::string &mode,
                          uint16_t &port,
                          std::vector<std::string> &hosts) {

  // Get the string from the config file
  const std::string str = conf.Get(group, mode, "");
  if (str == "") {
    return false;
  }

  // Split the port from the list of hosts
  std::vector<std::string> port_hosts;
  splitstr(str, port_hosts, ':');
  port = atoi(port_hosts[0].c_str());
 
  // Parse the host list if there is one
  hosts.clear();
  if (port_hosts.size() == 1) {
    hosts.push_back("127.0.0.1");
    return true;
  }
  splitstr(port_hosts[1], hosts, ',');

  return true;
}
