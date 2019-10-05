
#include <logging.hh>

namespace sinks=boost::log::sinks;
namespace exper=boost::log::expressions;
namespace kwrds=boost::log::keywords;


// Attribute value tag type
struct severity_tag;

// The operator is used when putting the severity level to log
boost::log::formatting_ostream& operator<<
(
 boost::log::formatting_ostream& strm,
 boost::log::to_log_manip<Logger::severity_level, severity_tag> const& manip
) {
  static const char *strings[] = { "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG" };
  Logger::severity_level level = manip.get();
  if (static_cast<std::size_t>(level) < sizeof(strings) / sizeof(*strings)) {
    strm << strings[level];
  } else {
    strm << static_cast<int>(level);
  }
  return strm;
}

Logger::Logger(const std::string &log_level_str,
	       const std::string &syslog_level_str,
	       const std::string &loghost) {
  severity_level console_log_level = parse_log_level(log_level_str);
  severity_level syslog_log_level = parse_log_level(syslog_level_str);

  boost::log::register_simple_formatter_factory<severity_level, char>("Severity");

  // Creating a syslog sink.
  boost::shared_ptr<sinks::synchronous_sink<sinks::syslog_backend> > sink;
  sink.reset(new sinks::synchronous_sink<sinks::syslog_backend>
	     (kwrds::facility = sinks::syslog::local0,
	      kwrds::use_impl = sinks::syslog::impl_types::native
	      ) );
  //sink->set_formatter(exper::format(name + ": %1%") % exper::message);

  // Mapping our custom levels to the syslog levels.
  sinks::syslog::custom_severity_mapping<severity_level> mapping("Severity");
  mapping[critical] = sinks::syslog::critical;
  mapping[error] = sinks::syslog::error;
  mapping[warning] = sinks::syslog::warning;
  mapping[info] = sinks::syslog::info;
  mapping[debug] = sinks::syslog::debug;
  sink->locked_backend()->set_severity_mapper(mapping);
  sink->set_filter(exper::attr<severity_level>("Severity") <= syslog_log_level);
  sink->set_formatter(exper::stream << exper::attr<severity_level, severity_tag>("Severity") <<
		      ": " << exper::smessage);

  // Setting the remote address to sent syslog messages to.
  sink->locked_backend()->set_target_address(loghost);

  // Adding the sink to the core.
  boost::log::core::get()->add_sink(sink);

  // Add logging to the console
  boost::log::add_console_log(std::clog,
			      kwrds::filter = exper::attr<severity_level>("Severity") <=
			      console_log_level,
			      kwrds::format =
			      ( exper::stream <<
				exper::attr<severity_level, severity_tag>("Severity") <<
				": " << exper::smessage )
			      );
}

void Logger::create(const std::string &log_level,
		    const std::string &syslog_level,
		    const std::string &loghost) {
  if (!g_logger) {
    g_logger.reset(new Logger(log_level, syslog_level, loghost));
  }
}

Logger::severity_level Logger::parse_log_level(const std::string &str) {
  if (str == "critical") {
    return critical;
  } else if(str == "error") {
    return error;
  } else if(str == "warning") {
    return warning;
  } else if(str == "info") {
    return info;
  } else if(str == "debug") {
    return debug;
  }
  return critical;
}
