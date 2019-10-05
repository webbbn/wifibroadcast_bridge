
#pragma once

#define BOOST_LOG_USE_NATIVE_SYSLOG 1

#include <memory>

#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/utility/setup/console.hpp>

class Logger {
public:

  // Define application-specific severity levels
  enum severity_level { critical, error, warning, info, debug };

  typedef std::shared_ptr<Logger> LoggerP;
  typedef boost::log::sinks::synchronous_sink<boost::log::sinks::syslog_backend> Sink;
  typedef boost::log::sources::severity_logger<Logger::severity_level> BoostLogger;

  static void create(const std::string &log_level = "",
		     const std::string &syslog_level = "",
		     const std::string &loghost = "localhost");

  static BoostLogger &logger() {
    return g_logger->m_logger;
  }

private:
  BoostLogger m_logger;
  static LoggerP g_logger;

  Logger(const std::string &log_level_str = "",
	 const std::string &syslog_level_str = "",
	 const std::string &loghost = "localhost");

  severity_level parse_log_level(const std::string &str);
};

#define LOG_CRITICAL BOOST_LOG_SEV(Logger::logger(), Logger::critical)
#define LOG_ERROR BOOST_LOG_SEV(Logger::logger(), Logger::error)
#define LOG_WARNING BOOST_LOG_SEV(Logger::logger(), Logger::warning)
#define LOG_INFO BOOST_LOG_SEV(Logger::logger(), Logger::info)
#define LOG_DEBUG BOOST_LOG_SEV(Logger::logger(), Logger::debug)
