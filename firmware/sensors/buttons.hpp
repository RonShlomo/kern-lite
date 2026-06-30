#pragma once

namespace kern::sensors {
	enum class PressType {
		None,
		Short,
		Long };

	class Buttons {
	public:

		void init() {}

		PressType pollSw1()
		{
			return PressType::None;
		}
	};
}
