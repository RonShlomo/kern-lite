#include "state_machine.hpp"

namespace kern::recorder {
	bool StateMachine::process(Event event)
	{
		if (isIdle()) {
			switch (event) {
				case Event::ChecksPassed:
					m_state = State::Idle;
					return true;

				case Event::UartStart:
					m_state = State::Recording;
					return true;

				case Event::SdFault:
					m_state = State::Fault;
					return true;

				default:
					return false;
			}
		} else if (isLogging()) {
			switch (event) {
				case Event::UartStop:
					m_state = State::Idle;
					return true;

				case Event::ShortPress:
					m_state = State::Idle;
					return true;

				case Event::SdFault:
					m_state = State::Fault;
					return true;

				default:
					return false;
			}
		} else if (isFault()){
			switch (event) {
				case Event::FaultCleared:
					m_state = State::Recording;
					return true;

				default:
					return false;
			}
		}
		return false;
	}
} // namespace  kern::recorder
