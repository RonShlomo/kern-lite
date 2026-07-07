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

		m_rxQueue = xQueueCreateStatic(
		    kRxQueueLength,
		    sizeof(protocol::Frame),
		    m_rxQueueStorage,
		    &m_rxQueueControl
		);

		HAL_UART_Receive_IT(m_uart, &m_rxByte, 1);
	}

	void CommLink::feed(uint8_t byte)
	{
	    BaseType_t higherPriorityTaskWoken = pdFALSE;
	    feedFromISR(byte, &higherPriorityTaskWoken);
	}

	void CommLink::feedFromISR(uint8_t byte, BaseType_t* higherPriorityTaskWoken)
	{
	    protocol::DecodeResult result = m_decoder.feed(byte);

	    if (result == protocol::DecodeResult::FrameReady) {
	        protocol::Frame ready = m_decoder.frame();

	        if (m_rxQueue != nullptr) {
	            if (xQueueSendFromISR(m_rxQueue, &ready, higherPriorityTaskWoken) != pdTRUE) {
	                ++m_rxDropped;
	            }
	        }
	    }
	}

	bool CommLink::poll(protocol::Frame& out)
	{
	    if (m_rxQueue == nullptr) {
	        return false;
	    }

	    return xQueueReceive(m_rxQueue, &out, 0) == pdTRUE;
	}

	bool CommLink::receive(protocol::Frame& out, TickType_t timeoutTicks)
	{
	    if (m_rxQueue == nullptr) {
	        return false;
	    }

	    return xQueueReceive(m_rxQueue, &out, timeoutTicks) == pdTRUE;
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
	    BaseType_t higherPriorityTaskWoken = pdFALSE;

	    feedFromISR(m_rxByte, &higherPriorityTaskWoken);

	    HAL_UART_Receive_IT(m_uart, &m_rxByte, 1);

	    portYIELD_FROM_ISR(higherPriorityTaskWoken);
	}

} // end namespace kern::recorder

// Hardware Interrupt Callback (C-linkage)
// Overrides the HAL's weak C-callback to bridge hardware interrupts into our C++ environment.
// It routes the newly received UART byte directly into the global CommLink instance.

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART2 && g_commLink != nullptr) {
        g_commLink->handleRxISR();
    }
}
