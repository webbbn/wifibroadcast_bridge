
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <fstream>

#include <logging.hh>
#include <udp_receive.hh>

extern double last_packet_time;

int open_udp_socket_for_rx(uint16_t port, const std::string hostname, uint32_t timeout_us) {

  // Try to open a UDP socket.
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    LOG_ERROR << "Error opening the UDP receive socket.";
    return -1;
  }

  // Set the socket options.
  int optval = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const void *)&optval, sizeof(optval));

  // Set a timeout to ensure that the end of a frame gets flushed
  if (timeout_us > 0) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_us;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
  }

  // Find to the receive port
  struct sockaddr_in saddr;
  bzero((char *)&saddr, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);

  // Lookup the IP address from the hostname
  std::string ip;
  if (hostname != "") {
    ip = hostname_to_ip(hostname);
    saddr.sin_addr.s_addr = inet_addr(ip.c_str());
  } else {
    saddr.sin_addr.s_addr = INADDR_ANY;
  }

  if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    LOG_ERROR << "Error binding to the UDP receive socket: " << port;
    return -1;
  }

  return fd;
}

void udp_recv_loop(PacketQueueP outq, const std::string hostname, uint16_t port,
                   uint32_t blocksize) {

  // Try to open the UDP socket.
  int udp_sock = open_udp_socket_for_rx(port, hostname, 0);
  if (udp_sock < 0) {
    LOG_CRITICAL << "Error opening the UDP socket for: " << hostname << ":" << port;
    return;
  }

  while (1) {

    // Receive the next message.
    std::shared_ptr<std::vector<uint8_t> > msg(new std::vector<uint8_t>(blocksize));
    ssize_t count = recv(udp_sock, msg->data(), blocksize, 0);

    // Did we receive a message to send?
    if (count > 0) {
      msg->resize(count);
      outq->push(msg);
    };
  }
}
