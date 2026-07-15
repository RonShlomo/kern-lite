#pragma once

#include "../protocol/frame.hpp"
#include "../storage/circular_log.hpp"
#include "state_machine.hpp"

namespace kern::recorder {
	class CommLink;

	class CommandHandler {
	public:
		void init(CommLink* link,  StateMachine* stateMachine, storage::CircularLog* circularLog);

		void dispatch(const protocol::Frame& f);

	    void sendAck();
	    void sendNack(protocol::NackCode code);
		void sendStatus();

	private:
	    CommLink* m_link = nullptr;
	    StateMachine* m_stateMachine = nullptr;
	    storage::CircularLog* m_circularLog = nullptr;

	    static bool replayRecordCallback(const storage::SensorRecord& record, void* ctx);
	    void sendRecord(const storage::SensorRecord& record);
	};
} // namespace kern::recorder
