#pragma once

#include "../protocol/frame.hpp"

namespace kern::recorder {
	class CommLink;

	class CommandHandler {
	public:
		void init(CommLink* link);

		void dispatch(const protocol::Frame& f);

	    void sendAck();
	    void sendNack(protocol::NackCode code);
		void sendStatus();

	private:
	    CommLink* m_link = nullptr;
	};
} // namespace kern::recorder
