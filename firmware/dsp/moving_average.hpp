#pragma once

#include <cstdint>
#include <cstring>

namespace kern::dsp {

template  <typename T, size_t N>
class MovingAverage {
	static_assert(N > 0, "Window size must be > 0");
public:

	MovingAverage()
	{
		reset();
	}

	T update(T sample)
	{
		m_sum -= m_buf[m_head];
		m_buf[m_head] = sample;
		m_sum += sample;
		m_head = (m_head + 1) % N;
		if (m_count < N) {
			++m_count;
		}
		return value();
	}

	T value() const
	{
		if (m_count == 0) {
			return T{};
		}
		return m_sum / static_cast<T>(m_count);
	}

	void reset()
	{
		memset(m_buf, 0, sizeof(m_buf));
		m_sum = T{};
		m_head = 0;
		m_count = 0;
	}

	size_t count() const
	{
		return m_count;
	}

	bool full() const
	{
		return m_count == N;
	}

private:
	T m_buf[N];
	T m_sum;
	size_t m_head;
	size_t m_count;

};

} //namespace kern::dsp

