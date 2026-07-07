#pragma once

namespace kern::sensors {
	class Dht11 {
	public:
		enum class Status {
			Ok,
			Timeout,
			CrcError
		};

		void init();

		Status read(float& tempC, float& humidity);

	};
}

