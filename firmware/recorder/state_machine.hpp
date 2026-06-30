#pragma once

#include <cstdint>

namespace kern::recorder {
	enum class State : uint8_t {
		Idle=0,
				Recording=1,
				Fault=2
	};

	enum class Event : uint8_t {
		ChecksPassed,
		SdFault,
		UartStart,
		UartStop,
		ShortPress,
		FaultCleared
	};

	class StateMachine {
	public:
		bool process(Event);
		State state() const
		{
			return m_state;
		}

		bool isIdle() const
		{
			return m_state == State::Idle;
		}

		bool isLogging() const
		{
			return m_state == State::Recording;
		}

		bool isFault() const
		{
			return m_state == State::Fault;
		}

	private:
		State m_state = State::Idle;
	};
}
