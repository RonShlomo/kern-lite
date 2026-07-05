#pragma once

#include "../protocol/frame.hpp"
#include "../protocol/codec.hpp"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <cstdint>


namespace kern::recorder {
	class CommLink {
	public:
		// ctor that takes the hardware UART handle
		explicit CommLink(UART_HandleTypeDef* uart);

		void init();
		void feed(uint8_t byte);
		bool poll(protocol::Frame& out);
		void send(const protocol::Frame& f);

		// helper method that the ISR callback will call
		// Overrides the HAL's weak C-callback to bridge hardware interrupts into our C++ environment.
		void handleRxISR();

	private:
		UART_HandleTypeDef* m_uart;
		protocol::Decoder m_decoder;

		// holds the fully decoded frame until the Comms task is ready to get it
		protocol::Frame m_pending{};

		// flag to indicate a new frame is waiting (volatile because its modified in an ISR)
		volatile bool m_frameReady = false;

		// static mutex
		StaticSemaphore_t m_txMutexBuffer;
		SemaphoreHandle_t m_txMutex = nullptr;

		// buffer to recieve exactly 1 byte from the UART
		uint8_t m_rxByte;
	};
}

// global pointer required by the spec so the C style ISR can find the C++ object
extern kern::recorder::CommLink* g_commLink;
