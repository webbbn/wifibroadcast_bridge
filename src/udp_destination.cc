
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <logging.hh>
#include <udp_destination.hh>

std::string hostname_to_ip(const std::string &hostname) {

  // Try to lookup the host.
  struct hostent *he;
  if ((he = gethostbyname(hostname.c_str())) == NULL) {
    LOG_ERROR << "Error: invalid hostname";
    return "";
  }

  struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
  for(int i = 0; addr_list[i] != NULL; i++) {
    //Return the first one;
    return inet_ntoa(*addr_list[i]);
  }

  return "";
}

void splitstr(const std::string& str, std::vector<std::string> &tokens, char delim) {
  std::istringstream iss(str);
  std::string token;
  while (std::getline(iss, token, delim)) {
    tokens.push_back(token);
  }
}

UDPDestination::UDPDestination(const std::string &outports_str, std::shared_ptr<FECDecoder> enc,
			       bool is_status) :
  m_fec(enc), m_is_status(is_status) {

  // Get the remote hostname/ip(s) and port(s)
  std::vector<std::string> outports;
  splitstr(outports_str, outports, ',');
  m_socks.resize(outports.size());

  // Split out the hostnames and ports
  for (size_t i = 0; i < outports.size(); ++i) {
    std::vector<std::string> host_port;
    splitstr(outports[i], host_port, ':');
    if (host_port.size() != 2) {
      LOG_CRITICAL << "Invalid host:port specified (" << outports[i] << ")";
      return;
    }

    // Initialize the UDP output socket.
    const std::string &hostname = host_port[0];
    struct sockaddr_in &s = m_socks[i];
    uint16_t port = std::stoi(host_port[1]);
    memset(&s, '\0', sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = (in_port_t)htons(port);

    // Lookup the IP address from the hostname
    std::string ip;
    if (hostname != "") {
      ip = hostname_to_ip(hostname);
      s.sin_addr.s_addr = inet_addr(ip.c_str());
    } else {
      s.sin_addr.s_addr = INADDR_ANY;
    }
    s.sin_addr.s_addr = inet_addr(ip.c_str());
  }
}

void UDPDestination::send(int send_sock, const uint8_t* buf, size_t len) {
  for (const auto &s : m_socks) {
    sendto(send_sock, buf, len, 0, (struct sockaddr *)&(s), sizeof(struct sockaddr_in));
  }
}
