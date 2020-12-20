
#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 

#include <logging.hh>
#include <wifibroadcast/raw_socket.hh>
#include <control_server.hh>

bool ControlServer::start(uint16_t port) {

  // Create the socket
  if ((m_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
    LOG_ERROR << "Creating the ControlServer socket failed";
    return false;
  }

  // Configure the socket
  int opt = 1; 
  if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    LOG_ERROR << "Setting the socket options in ControlServer failed";
    return false;
  } 

  // Bind to the socket
  struct sockaddr_in address; 
  int addrlen = sizeof(address); 
  address.sin_family = AF_INET; 
  address.sin_addr.s_addr = INADDR_ANY; 
  address.sin_port = htons(port);
  if (bind(m_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    LOG_ERROR << "Binding to port " << port << " in ControlServer failed";
    return false;
  }

  // Start the listen thread
  m_listen_thread.reset(new std::thread([this] () { this->listen_thread(); }));

  return true;
}

void ControlServer::listen_thread() {

  // Continue listening for new connections and start server threads to service them
  struct sockaddr_in address; 
  int addrlen = sizeof(address); 
  while (listen(m_fd, 2) >= 0) {
    int sock_fd = accept(m_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (sock_fd < 0) {
      LOG_ERROR << "Error accepting a connection in ControlServer";
      continue;
    }

    // Spawn a thread to service this connection
    m_threads.push_back(std::make_shared<std::thread>
                        ([this, sock_fd] () { this->service_thread(sock_fd); })
                        );
  }
}

void ControlServer::service_thread(int fd) {
  char buffer[4096];
  int len = 0;
  while((len = read(fd, buffer, 4096)) > 0) {
    if (len < 2) {
      LOG_ERROR << "ControlServer command is too short";
      continue;
    }
    std::string cmd(buffer, len);

    // Remove newlines and CR
    if (cmd[len - 1] == '\n') {
      cmd = cmd.substr(0, len - 1);
    }
    if (cmd[len - 2] == '\r') {
      cmd = cmd.substr(0, len - 2);
    }

    // Switch on commands
    uint16_t freq;
    std::string response;
    std::string error;
    if (cmd == "FREQS") {
      std::stringstream fstr;
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto freq : m_freqs) {
          fstr << freq << std::endl;
        }
      }
      if (write(fd, fstr.str().c_str(), fstr.str().length()) != fstr.str().length()) {
        error = "Error sending frequency list in ControlServer";
      }
    } else if (sscanf(cmd.c_str(), "SETFREQ %hd", &freq) == 1) {
      LOG_INFO << "Setting the frequency to " << freq;
      if (set_wifi_frequency(m_device, freq)) {
        response = "OK\n";
      } else {
        std::stringstream ss;
        ss << "Error setting frequency to " << freq << "\n";
        error = ss.str();
      }
    } else {
      error = "Invalid command: <" + cmd + ">\n";
    }

    if (error.length()) {
      if (write(fd, error.c_str(), error.length()) != error.length()) {
        LOG_ERROR << "Error sending error message: " << error;
      }
    } else if(response.length()) {
      if (write(fd, response.c_str(), response.length()) != response.length()) {
        LOG_ERROR << "Error sending response message: " << response;
      }
    }
  }
}

void ControlServer::update_frequencies(const std::string &device) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_device = device;
  get_wifi_frequency_list(device, m_freqs);
}

void ControlServer::get_frequencies(std::vector<uint32_t> &freq) {
  freq.resize(m_freqs.size());
  std::copy(m_freqs.begin(), m_freqs.end(), freq.begin());
}
