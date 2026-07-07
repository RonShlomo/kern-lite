#include "moving_average.hpp"
#include "threshold_detector.hpp"

namespace kern::dsp {

// DSP pipeline for single sensor channel
template <size_t N>
class Channel {
public:
	struct Out {
		float raw;
		float filtered;
		ThresholdDetector::State alert;
	};

	explicit Channel(ThresholdConfig cfg)
	: m_detector(cfg)
	{}

	Channel() = default;

	Out process(float raw)
	{
		float filtered = m_avg.update(raw);
		auto alert = m_detector.update(filtered);
		return Out{raw, filtered, alert};
	}

	void configure(ThresholdConfig cfg)
	{
		m_detector.configure(cfg);
	}

	void reset()
	{
		m_detector.reset();
	}

	float filtered() const
	{
		return m_avg.value();
	}

	ThresholdDetector::State alert() const
	{
		return m_detector.state();
	}

	bool isAlert() const
	{
		if (!m_avg.full()) {
			return false;
		}
		return m_detector.isAlert();
	}

private:
	MovingAverage<float, N> m_avg;
	ThresholdDetector m_detector;
};

} // namespace kern::channel

