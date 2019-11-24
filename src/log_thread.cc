
#include <iomanip>
#include <thread>

#include <boost/format.hpp>

#include <logging.hh>
#include <log_thread.hh>

template <typename tmpl__T>
double mbps(tmpl__T v1, tmpl__T v2, double time) {
  double diff = static_cast<double>(v1) - static_cast<double>(v2);
  return diff * 8e-6 / time;
}
template <typename tmpl__T>
uint32_t kbps(tmpl__T v1, tmpl__T v2, double time) {
  double diff = static_cast<double>(v1) - static_cast<double>(v2);
  return static_cast<uint32_t>(std::round(diff * 8e-3 / time));
}

// Send status messages to the other radio and log the FEC stats periodically
void log_thread(TransferStats &stats, TransferStats &stats_other, float syslog_period,
		float status_period, SharedQueue<std::shared_ptr<Message> > &outqueue,
		std::shared_ptr<Message> msg, std::shared_ptr<UDPDestination> udp_out,
		std::shared_ptr<UDPDestination> packed_udp_out) {
  bool is_ground = (stats.name() == "ground");

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
  transfer_stats_t pps = stats.get_stats();
  double last_stat = cur_time();
  double last_log = cur_time();
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(loop_period));
    double t = cur_time();

    // Send status if it's time
    double stat_dur = t - last_stat;
    if (stat_dur > status_period) {

      // I don't see any reason to send status upstream currently.
      if (!is_ground) {
	std::shared_ptr<Message> omsg = msg->create(stats.serialize());
	outqueue.push(omsg);
      }

      // Send the local status out the UDP port
      std::string outmsg = stats.serialize();
      sendto(send_sock, outmsg.c_str(), outmsg.length(), 0,
	     (struct sockaddr *)&(udp_out->s), sizeof(struct sockaddr_in));

      // Create the packed status message and send it.
      if (packed_udp_out) {
	transfer_stats_t s = stats.get_stats();
	transfer_stats_t os = stats_other.get_stats();
	wifibroadcast_rx_status_forward_t rxs;
	rxs.damaged_block_cnt = s.block_errors;
	rxs.lost_packet_cnt = s.sequence_errors;
	rxs.skipped_packet_cnt = 0;
	rxs.injection_fail_cnt = os.inject_errors;
	rxs.received_packet_cnt = s.blocks_in;
	rxs.kbitrate = kbps(s.bytes_in, pps.bytes_in, stat_dur);
	rxs.kbitrate_measured = 0;
	rxs.kbitrate_set = 0;
	rxs.lost_packet_cnt_telemetry_up = os.block_errors;
	rxs.lost_packet_cnt_telemetry_down = 0;
	rxs.lost_packet_cnt_msp_up = 0;
	rxs.lost_packet_cnt_msp_down = 0;
	rxs.lost_packet_cnt_rc = 0;
	rxs.current_signal_joystick_uplink = os.rssi;
	rxs.current_signal_telemetry_uplink = os.rssi;
	rxs.joystick_connected = 0;
	rxs.HomeLat = 0;
	rxs.HomeLon = 0;
	rxs.cpuload_gnd = 0;
	rxs.temp_gnd = 0;
	rxs.cpuload_air = 0;
	rxs.temp_air = 0;
	rxs.wifi_adapter_cnt = 1;
	rxs.adapter[0].received_packet_cnt = s.blocks_in;
	rxs.adapter[0].current_signal_dbm = s.rssi;
	rxs.adapter[0].type = 1;
	rxs.adapter[0].signal_good = (s.rssi > -100);

	sendto(send_sock, reinterpret_cast<uint8_t*>(&rxs), sizeof(rxs), 0,
	       (struct sockaddr *)&(packed_udp_out->s), sizeof(struct sockaddr_in));
	pps = s;
      }

      last_stat = t;
    }

    // Post a log message if it's time
    double log_dur = t - last_log;
    if (log_dur >= syslog_period) {
      transfer_stats_t s = stats.get_stats();
      std::string blks = is_ground ?
	(boost::str(boost::format("%4d %4d %4d") % 
		    (s.blocks_in - ps.blocks_in) %
		    (s.blocks_out - ps.blocks_out) %
		    (s.block_errors - ps.block_errors))) :
	(boost::str(boost::format("%4d %4d %4d") % 
		    (s.blocks_out - ps.blocks_out) %
		    (s.blocks_in - ps.blocks_in) %
		    (s.block_errors - ps.block_errors)));
      std::string times = boost::str(boost::format("%3d %3d %3d") % 
				     static_cast<uint32_t>(std::round(s.encode_time)) %
				     static_cast<uint32_t>(std::round(s.send_time)) %
				     static_cast<uint32_t>(std::round(s.pkt_time)));
      LOG_INFO << "Name   Interval Seq(r/e)  Blocks(d/u/e)  Inj  Rate(d/u)   Times(e/s/t)Us  LatMs RSSI";
      LOG_INFO << boost::format
	("%-6s %4.2f s %4d %4d   %-14s %3d %5.2f %5.2f  %14s  %3d   %4d") %
	stats.name() %
	std::round(log_dur) %
	(s.sequences - ps.sequences) %
	(s.sequence_errors - ps.sequence_errors) %
	blks %
	(s.inject_errors - ps.inject_errors) %
	mbps(s.bytes_in, ps.bytes_in, log_dur) %
	mbps(s.bytes_out, ps.bytes_out, log_dur) %
	times %
	static_cast<uint32_t>(std::round(s.latency)) %
	static_cast<int16_t>(std::round(s.rssi));
      ps = s;
      transfer_stats_t so = stats_other.get_stats();
      std::string oblks = is_ground ?
	(boost::str(boost::format("%4d %4d %4d") % 
		    (so.blocks_out - pso.blocks_out) %
		    (so.blocks_in - pso.blocks_in) %
		    (so.block_errors - pso.block_errors))) :
	(boost::str(boost::format("%4d %4d %4d") % 
		    (so.blocks_in - pso.blocks_in) %
		    (so.blocks_out - pso.blocks_out) %
		    (so.block_errors - pso.block_errors)));
      std::string otimes = boost::str(boost::format("%3d %3d %3d") % 
				      static_cast<uint32_t>(std::round(so.encode_time)) %
				      static_cast<uint32_t>(std::round(so.send_time)) %
				      static_cast<uint32_t>(std::round(so.pkt_time)));
      LOG_INFO << boost::format
	("%-6s %4.2f s %4d %4d   %-14s %3d %5.2f %5.2f  %14s  %3d   %4d") %
	stats_other.name() %
	std::round(log_dur) %
	(so.sequences - pso.sequences) %
	(so.sequence_errors - pso.sequence_errors) %
	oblks %
	(so.inject_errors - pso.inject_errors) %
	mbps(so.bytes_out, pso.bytes_out, log_dur) %
	mbps(so.bytes_in, pso.bytes_in, log_dur) %
	otimes %
	static_cast<uint32_t>(std::round(so.latency)) %
	static_cast<int16_t>(std::round(so.rssi));
      pso = so;
      last_log = t;
    }
  }
}
