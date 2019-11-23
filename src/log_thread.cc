
#include <iomanip>
#include <thread>

#include <logging.hh>
#include <log_thread.hh>

template <typename tmpl__T>
double mbps(tmpl__T v1, tmpl__T v2, double time) {
  double diff = static_cast<double>(v1) - static_cast<double>(v2);
  return diff * 8e-6 / time;
}

// Send status messages to the other radio and log the FEC stats periodically
void log_thread(TransferStats &stats, TransferStats &stats_other, float syslog_period,
		float status_period, SharedQueue<std::shared_ptr<Message> > &outqueue,
		std::shared_ptr<Message> msg, std::shared_ptr<UDPDestination> udp_out,
		std::shared_ptr<UDPDestination> packed_udp_out) {

  // Open the UDP send socket
  int send_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (send_sock < 0) {
    LOG_CRITICAL << "Error opening the UDP send socket in log_thread.";
    return;
  }

  uint32_t loop_period =
    static_cast<uint32_t>(std::round(1000.0 * ((syslog_period == 0) ? status_period :
					       ((status_period == 0) ? syslog_period :
						std::min(syslog_period, status_period)))));
  transfer_stats_t ps = stats.get_stats();
  transfer_stats_t pso = stats_other.get_stats();
  double last_stat = cur_time();
  double last_log = cur_time();
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(loop_period));
    double t = cur_time();

    // Send status if it's time
    double stat_dur = t - last_stat;
    if (stat_dur > status_period) {
      std::shared_ptr<Message> omsg = msg->create(stats.serialize());
      outqueue.push(omsg);

      // Send the local status out the UDP port
      std::string outmsg = stats.serialize();
      sendto(send_sock, outmsg.c_str(), outmsg.length(), 0,
	     (struct sockaddr *)&(udp_out->s), sizeof(struct sockaddr_in));

      // Create the packed status message and send it.
      if (packed_udp_out) {
	transfer_stats_t s = stats.get_stats();
	transfer_stats_t os = stats_other.get_stats();
	wifibroadcast_rx_status_forward_t ps;
	ps.damaged_block_cnt = s.block_errors;
	ps.lost_packet_cnt = s.sequence_errors;
	ps.skipped_packet_cnt = 0;
	ps.injection_fail_cnt = os.inject_errors;
	ps.received_packet_cnt = s.blocks_in;
	ps.kbitrate = 8 * s.bytes_in / 1000;
	ps.kbitrate_measured = 0;
	ps.kbitrate_set = 0;
	ps.lost_packet_cnt_telemetry_up = os.block_errors;
	ps.lost_packet_cnt_telemetry_down = 0;
	ps.lost_packet_cnt_msp_up = 0;
	ps.lost_packet_cnt_msp_down = 0;
	ps.lost_packet_cnt_rc = 0;
	ps.current_signal_joystick_uplink = os.rssi;
	ps.current_signal_telemetry_uplink = os.rssi;
	ps.joystick_connected = 0;
	ps.cpuload_gnd = 0;
	ps.temp_gnd = 0;
	ps.cpuload_air = 0;
	ps.temp_air = 0;
	ps.wifi_adapter_cnt = 1;
	ps.adapter[0].received_packet_cnt = s.blocks_in;
	ps.adapter[0].current_signal_dbm = s.rssi;
	ps.adapter[0].type = 1;
	ps.adapter[0].signal_good = (s.rssi > -100);

	sendto(send_sock, reinterpret_cast<uint8_t*>(&ps), sizeof(ps), 0,
	       (struct sockaddr *)&(packed_udp_out->s), sizeof(struct sockaddr_in));
      }

      last_stat = t;
    }

    // Post a log message if it's time
    double log_dur = t - last_log;
    if (log_dur >= syslog_period) {
      transfer_stats_t s = stats.get_stats();
      LOG_INFO
	<< std::setprecision(3)
	<< stats.name() << ":  "
	<< "int: " << log_dur << "  "
	<< "seq: " << (s.sequences - ps.sequences) << "/"
	<< (s.sequence_errors - ps.sequence_errors) << "  "
	<< "blk s,r: " << (s.blocks_out - ps.blocks_out) << "/"
	<< s.inject_errors - ps.inject_errors << " "
	<< (s.blocks_in - ps.blocks_in) << "/"
	<< s.block_errors - ps.block_errors << "  "
	<< "rate s,r: " << mbps(s.bytes_out, ps.bytes_out, log_dur) << "/"
	<< mbps(s.bytes_in, ps.bytes_in, log_dur) << " Mbps"
	<< "  times (e/s/t): " << s.encode_time << "/"<< s.send_time << "/"
	<< s.pkt_time << " us"
	<< "  lat: " << s.latency << " ms"
	<< "  RSSI: " << static_cast<int16_t>(std::round(s.rssi));
      ps = s;
      transfer_stats_t so = stats_other.get_stats();
      LOG_INFO
	<< std::setprecision(3)
	<< stats_other.name() << ":  "
	<< "int: " << log_dur << "  "
	<< "seq: " << (so.sequences - pso.sequences) << "/"
	<< (so.sequence_errors - pso.sequence_errors) << "  "
	<< "blk s,r: " << (so.blocks_out - pso.blocks_out) << "/"
	<< so.inject_errors - pso.inject_errors << " "
	<< (so.blocks_in - pso.blocks_in) << "/"
	<< so.block_errors - pso.block_errors << "  "
	<< "rate s,r: " << mbps(so.bytes_out, pso.bytes_out, log_dur) << "/"
	<< mbps(so.bytes_in, pso.bytes_in, log_dur) << " Mbps"
	<< "  times (e/s/t): " << so.encode_time << "/"	<< so.send_time << "/"
	<< so.pkt_time << " us"
	<< "  lat: " << so.latency << " ms"
	<< "  RSSI: " << static_cast<int16_t>(std::round(so.rssi));
      pso = so;
      last_log = t;
    }
  }
}
