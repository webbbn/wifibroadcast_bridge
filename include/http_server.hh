#pragma once

#include <string.h> 

#include <thread>
#include <memory>
#include <mutex>

class CivetServer;
class CivetHandler;

class HTTPServer {
public:

  HTTPServer() {}

  bool start(uint16_t port, const std::string &document_root="");

  bool update_frequencies(const std::string &device, uint32_t final_freq);
  void get_frequencies(std::vector<uint32_t> &freq);
  void get_frequencies(std::set<uint32_t> &freq);
  bool frequency_valid(uint32_t freq);
  bool set_frequency(uint32_t freq);
  uint32_t get_frequency();

private:
  std::shared_ptr<CivetServer> m_server;
  std::vector<std::shared_ptr<CivetHandler> > m_handlers;
  std::mutex m_mutex;
  std::vector<uint32_t> m_freqs;
  std::string m_device;
  uint16_t m_current_frequency;
};
