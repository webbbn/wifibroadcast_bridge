
#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 

#include <CivetServer.h>

#include <logging.hh>
#include <wifibroadcast/raw_socket.hh>
#include <http_server.hh>

class FrequencyListHandler : public CivetHandler {
public:
  FrequencyListHandler(HTTPServer *server) : m_server(server) {}
  virtual ~FrequencyListHandler() {}

  virtual bool handleGet(CivetServer *server, struct mg_connection *conn) {
    std::vector<uint32_t> freqs;
    m_server->get_frequencies(freqs);
    for (auto freq : freqs) {
      mg_printf(conn, "%d\r\n", freq);
    }
    return true;
  }

protected:
  HTTPServer *m_server;
};

class SetFrequencyHandler : public CivetHandler {
public:
  SetFrequencyHandler(HTTPServer *server) : m_server(server) {}
  virtual ~SetFrequencyHandler() {}

  virtual bool handleGet(CivetServer *server, struct mg_connection *conn) {
    std::string freqstr = "";
    uint32_t freq;
    if (!CivetServer::getParam(conn, "value", freqstr) ||
        (sscanf(freqstr.c_str(), "%d", &freq) != 1) ||
        !m_server->frequency_valid(freq)) {
      mg_printf(conn, "INVALID");
    } else {
      mg_printf(conn, "OK");
      LOG_INFO << "Changing frequency to " << freq;
      m_server->set_frequency(freq);
    }
    return true;
  }

protected:
  HTTPServer *m_server;
};

class GetFrequencyHandler : public CivetHandler {
public:
  GetFrequencyHandler(HTTPServer *server) : m_server(server) {}
  virtual ~GetFrequencyHandler() {}

  virtual bool handleGet(CivetServer *server, struct mg_connection *conn) {
    uint32_t freq = m_server->get_frequency();
    mg_printf(conn, "%d", freq);
    return true;
  }

protected:
  HTTPServer *m_server;
};

bool HTTPServer::start(uint16_t port, const std::string &document_root) {

  // Initialize the web server library
  mg_init_library(0);

  // Construct the web server options.
  std::vector<const char*> options;
  options.push_back("listening_ports");
  char portstr[20];
  sprintf(portstr, "%d", port);
  options.push_back(portstr);
  if (document_root != "") {
    options.push_back("document_root");
    options.push_back(document_root.c_str());
  }
  options.push_back(NULL);

  // Start the web server
  m_server = std::make_shared<CivetServer>(options.data());

  // Add the handlers
  m_handlers.push_back(std::make_shared<FrequencyListHandler>(this));
  m_server->addHandler("/frequencies", m_handlers.back().get());
  m_handlers.push_back(std::make_shared<SetFrequencyHandler>(this));
  m_server->addHandler("/set_frequency", m_handlers.back().get());
  m_handlers.push_back(std::make_shared<GetFrequencyHandler>(this));
  m_server->addHandler("/get_frequency", m_handlers.back().get());

  return true;
}

bool HTTPServer::update_frequencies(const std::string &device, uint32_t final_freq) {
  m_device = device;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    get_wifi_frequency_list(device, m_freqs);
  }
  return set_frequency(final_freq);
}

void HTTPServer::get_frequencies(std::vector<uint32_t> &freq) {
  std::lock_guard<std::mutex> lock(m_mutex);
  freq.resize(m_freqs.size());
  std::copy(m_freqs.begin(), m_freqs.end(), freq.begin());
}

void HTTPServer::get_frequencies(std::set<uint32_t> &freq) {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto f : m_freqs) {
    freq.insert(f);
  }
}

bool HTTPServer::frequency_valid(uint32_t freq) {
  std::set<uint32_t> freqs;
  get_frequencies(freqs);
  if (freqs.find(freq) != freqs.end()) {
    return true;
  }
  return false;
}

bool HTTPServer::set_frequency(uint32_t freq) {
  if (!frequency_valid(freq)) {
    return false;
  }
  if (!set_wifi_frequency(m_device, freq)) {
    LOG_ERROR << "Error setting frequency of " << m_device << " to " << freq;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_current_frequency = freq;
  }
  return true;
}

uint32_t HTTPServer::get_frequency() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_current_frequency;
}
