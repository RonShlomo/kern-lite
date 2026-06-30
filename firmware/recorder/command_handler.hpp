#pragma once

#include "../protocol/frame.hpp"

namespace kern::recorder {
	class CommandHandler {
	public:
		void dispatch(const protocol::Frame& f)
		{
			(void)f;
		}

		void sendStatus() {}
	};
}
