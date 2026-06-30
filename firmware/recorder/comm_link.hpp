#pragma once

#include "../protocol/frame.hpp"

namespace kern::recorder {
	class CommLink {
	public:
		void init() {}

		bool poll(protocol::Frame& out)
		{
			(void)out;
			return false;
		}

		void send(const protocol::Frame& f)
		{
			(void)f;
		}
	};
}
