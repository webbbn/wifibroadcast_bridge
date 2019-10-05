#ifndef SHARED_QUEUE_HH
#define SHARED_QUEUE_HH

#include <condition_variable>
#include <queue>
#include <mutex>

template <typename tmpl__T>
class SharedQueue {
public:
  SharedQueue() = default;
  ~SharedQueue() = default;

  tmpl__T pop() {
    std::unique_lock<std::mutex> lock_guard(m_mutex);

    while (m_queue.empty()) {
      m_cond.wait(lock_guard);
    }

    auto item = m_queue.front();
    m_queue.pop();
    return item;
  }

  void push(tmpl__T item) {
    std::unique_lock<std::mutex> lock_guard(m_mutex);
    m_queue.push(item);
    lock_guard.unlock();
    m_cond.notify_one();
  }

  size_t size() const {
    return m_queue.size();
  }

private:
  std::queue<tmpl__T> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_cond;
};

#endif // SHARED_QUEUE_HH
