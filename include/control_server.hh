#pragma once

#include <string.h> 

#include <thread>
#include <memory>
#include <mutex>

class ControlServer {
public:

  ControlServer() : m_fd(0) {}

  bool start(uint16_t port);

  void update_frequencies(const std::string &device);
  void get_frequencies(std::vector<uint32_t> &freq);

private:
  int m_fd;
  std::shared_ptr<std::thread> m_listen_thread;
  std::vector<std::shared_ptr<std::thread> > m_threads;
  std::mutex m_mutex;
  std::vector<uint32_t> m_freqs;
  std::string m_device;

  void listen_thread();
  void service_thread(int fd);
};
