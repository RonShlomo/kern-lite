#pragma once

namespace kern::dsp {

struct ThresholdConfig {
	float lo; // LowAlert below this
	float hi; // HighAlert above this
	float hysteresis; // must move this far back inside bounds to remove alert
};

class ThresholdDetector {
public:
	enum class State : uint8_t {
		Normal,
		LowAlert,
		HighAlert
	};

	explicit ThresholdDetector(ThresholdConfig cfg)
	: m_cfg(cfg)
	, m_state(State::Normal)
	{}

	ThresholdDetector()
	: m_cfg{0.0f, 0.0f, 0.0f}
	, m_state(State::Normal)
	{}

	State update(float v)
	{
		switch (m_state) {
			case State::Normal:
				if (v > m_cfg.hi) {
					m_state = State::HighAlert;
				}
				else if (v < m_cfg.lo) {
					m_state = State::LowAlert;
				}
				break;
			case State::HighAlert:
				if (v < m_cfg.hi - m_cfg.hysteresis) {
					m_state = (v < m_cfg.lo) ? State::LowAlert : State::Normal;
				}
				break;
			case State::LowAlert:
				if (v > m_cfg.lo + m_cfg.hysteresis) {
					m_state = (v > m_cfg.hi) ? State::HighAlert : State::Normal;
				}
				break;
		}
		return m_state;
	}

	State state() const
	{
		return m_state;
	}

	void reset()
	{
		m_state = State::Normal;
	}

	void configure(ThresholdConfig cfg)
	{
		m_cfg = cfg;
		m_state = State::Normal;
	}

	bool isAlert() const
	{
		return m_state != State::Normal;
	}

private:
	ThresholdConfig m_cfg;
	State m_state;
};

} // namespace kern::dsp

