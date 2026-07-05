#include "comm_link.hpp"

// global pointer initialized to nullptr
kern::recorder::CommLink* g_commLink = nullptr;

namespace kern::recorder {
	CommLink::CommLink(UART_HandleTypeDef* uart) : m_uart(uart)
	{
	}

	void CommLink::init()
	{
		// point the global pointer to this instance for the ISR callback
		g_commLink = this;

		m_txMutex = xSemaphoreCreateMutexStatic(&m_txMutexBuffer);
		HAL_UART_Receive_IT(m_uart, &m_rxByte, 1);
	}

	void CommLink::feed(uint8_t byte)
	{
		// runs inside the ISR, has to be fast and non blocking
		protocol::DecodeResult result = m_decoder.feed(byte);

		if (result == protocol::DecodeResult::FrameReady) {
			m_pending = m_decoder.frame();
			m_frameReady = true;
		}
	}

	bool CommLink::poll(protocol::Frame& out)
	{
		bool hasFrame = false;

		// disable hardware interrupts temporarily (begin atomic section)
		// so the UART ISR cannot overwrite m_pending while we are copying it.
		taskENTER_CRITICAL();

		if (m_frameReady) {
			out = m_pending;
			m_frameReady = false;
			hasFrame = true;
		}

		// enable hardware interrupts (end atomic section)
		taskEXIT_CRITICAL();

		return hasFrame;
	}

	void CommLink::send(const protocol::Frame& f)
	{
		// buffer on the stack to hold the raw bytes
		uint8_t txBuf[protocol::kFrameOverhead + protocol::kMaxPayload];

		// take the mutex. if another task is sending right now, this task will go to sleep
		// and wait until the Mutex is released (portMAX_DELAY means wait forever if necessary)
		xSemaphoreTake(m_txMutex, portMAX_DELAY);

		size_t bytesToWrite = protocol::encode(f, txBuf, sizeof(txBuf));

		if (bytesToWrite > 0) {
			// transmit over UART. since we hold the mutex, it's safe to use the blocking Transmit function
			HAL_UART_Transmit(m_uart, txBuf, bytesToWrite, HAL_MAX_DELAY);
		}

		// release the mutex
		xSemaphoreGive(m_txMutex);
	}

	void CommLink::handleRxISR()
	{
		feed(m_rxByte);

		// Crucial: Arm the UART to listen for the next byte so the UART will not go deaf after 1 byte.
		HAL_UART_Receive_IT(m_uart, &m_rxByte, 1);
	}

} // end namespace kern::recorder

// Hardware Interrupt Callback (C-linkage)
// Overrides the HAL's weak C-callback to bridge hardware interrupts into our C++ environment.
// It routes the newly received UART byte directly into the global CommLink instance.
extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    // Check if the global pointer is set and if the interrupt is from our specific UART
    if (g_commLink != nullptr) {
        // Route the interrupt into our C++ object
        g_commLink->handleRxISR();
    }
}
