#ifndef STATS_ACCUMULATOR_HH
#define STATS_ACCUMULATOR_HH

#include <limits>

#include <stdint.h>

template <typename tmpl__T>
class StatsAccumulator {
public:
  StatsAccumulator() {
    reset();
  }

  void add(tmpl__T v) {
    m_min = std::min(m_min, v);
    m_max = std::max(m_max, v);
    m_sum += v;
    ++m_count;
  }
  tmpl__T min() {
    return m_min;
  }
  tmpl__T max() {
    return m_max;
  }

  double sum() {
    return m_sum;
  }
  size_t count() {
    return m_count;
  }

  double mean() {
    return m_sum / m_count;
  }

  void reset() {
    m_min = std::numeric_limits<tmpl__T>::max();
    m_max = std::numeric_limits<tmpl__T>::lowest();
    m_sum = 0;
    m_count = 0;
  }

private:
  tmpl__T m_min;
  tmpl__T m_max;
  double m_sum;
  uint32_t m_count;
};

#endif // STATS_ACCUMULATOR_HH
