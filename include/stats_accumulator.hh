#ifndef STATS_ACCUMULATOR_HH
#define STATS_ACCUMULATOR_HH

#include <queue>
#include <algorithm>
#include <functional>
#include <numeric>

#include <stdint.h>
 

template <typename tmpl__T>
class StatsAccumulator {
public:
  StatsAccumulator(double time_window) : m_window(time_window) { }

  void add(tmpl__T v, double time) {
    m_q.push_front(std::make_pair(v, time));
    while (!m_q.empty()) {
      double qtime = m_q.back().second;
      if ((time - qtime) > m_window) {
	tmpl__T val = m_q.back().first;
	m_q.pop_back();
      } else if(m_q.size() > 100000) {
	std::cerr << m_q.size() << std::endl;
	break;
      } else {
	break;
      }
    }
  }
  tmpl__T min() {
    if (m_q.empty()) {
      return 0;
    }
    return std::min_element(m_q.begin(), m_q.end())->first;
  }
  tmpl__T max() {
    if (m_q.empty()) {
      return 0;
    }
    return std::max_element(m_q.begin(), m_q.end())->first;
  }

  double sum() {
    if (m_q.empty()) {
      return 0;
    }
    double ret = 0;
    for (const auto &p : m_q) {
      ret += p.first;
    }
    return ret;
  }
  size_t count() {
    return m_q.size();
  }

  double mean() {
    if (m_q.empty()) {
      return 0;
    }
    return sum() / m_q.size();
  }

private:
  double m_window;
  std::deque<std::pair<tmpl__T, double> > m_q;
};

#endif // STATS_ACCUMULATOR_HH
