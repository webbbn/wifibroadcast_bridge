
#include <map>

#include <logging.hh>
#include <wfb_bridge.hh>
#include <tun_interface.hh>

void tun_raw_thread(TUNInterface &tun_interface,
                    SharedQueue<std::shared_ptr<Message> > &outqueue,
                    std::map<uint16_t, std::shared_ptr<Message> > &port_lut,
                    uint32_t timeout_us) {
  double last_send_time = 0;

  while (1) {

    // Receive the next message.
    // 1ms timeout for FEC links to support flushing
    // Update rate target every 100 uS
    //uint32_t timeout_us = (rate_target > 0) ? 100 : (do_fec ? 1000 : 0);
    std::vector<uint8_t> msg(2048);
    uint16_t ip_port;
    tun_interface.read(msg, ip_port, timeout_us);

    // Did we receive a message to send?
    if (!msg.empty()) {

      // Lookup the IP port in the LUT.
      std::map<uint16_t, std::shared_ptr<Message> >::const_iterator itr = port_lut.find(ip_port);
      std::shared_ptr<Message> proto;
      if (itr != port_lut.end()) {
        proto = itr->second;
      } else if ((itr = port_lut.find(0)) != port_lut.end()) {
        proto = itr->second;
      } else {
        LOG_WARNING << "Did not find WFB port for IP port " << ip_port << " or default TUN port 0";
        continue;
      }

      // Add the mesage to the output queue
      outqueue.push(proto->copy(msg));

    } else {
      // flush all the encoders that are not flushed
      for (auto itr : port_lut) {
        auto proto = itr.second;
        if (!proto->enc->is_flushed()) {
          outqueue.push(proto->copy(std::vector<uint8_t>()));
        }
      }
    }
  }
}
