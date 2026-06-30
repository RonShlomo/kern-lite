#pragma once

namespace kern::sensors {
	class RadiationLatch {
		public:

		void init() {}
		void isr() {}
		bool consumeEvent()
		{
			return false;
		}
	};
}
