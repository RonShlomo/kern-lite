#ifndef COMMAND_HANDLER_CPP_
#define COMMAND_HANDLER_CPP_

#include "command_handler.hpp"
#include "comm_link.hpp"
#include <cstring>

namespace kern::recorder {

void CommandHandler::init(CommLink* link, StateMachine* stateMachine, storage::CircularLog* circularLog)
{
    m_link = link;
    m_stateMachine = stateMachine;
    m_circularLog = circularLog;
}

void CommandHandler::sendAck()
{
    if (m_link == nullptr) {
        return;
    }

    protocol::Frame out{};
    out.type = protocol::FrameType::Ack;
    out.len = 0;

    m_link->send(out);
}

void CommandHandler::sendNack(protocol::NackCode code)
{
    if (m_link == nullptr) {
        return;
    }

    protocol::Frame out{};
    out.type = protocol::FrameType::Nack;
    out.len = 1;
    out.payload[0] = static_cast<uint8_t>(code);

    m_link->send(out);
}

void CommandHandler::sendStatus()
{
    if (m_link == nullptr ||  m_stateMachine == nullptr || m_circularLog == nullptr) {
        return;
    }

    protocol::Frame out{};
    out.type = protocol::FrameType::Status;
    out.len = 14;

    const storage::StatusSnapshot snap = m_circularLog->snapshot();
    const uint32_t totalRecords = snap.totalRecords;
    const uint32_t wrapCount = snap.wrapCount;
    const uint16_t writeIndex = snap.writeIndex;

    // [0] state: 0 Idle, 1 Recording, 2 Fault
    out.payload[0] = static_cast<uint8_t>(m_stateMachine->state());

    // [1] sd_mounted
    out.payload[1] =  snap.mounted ? 1u : 0u;

    // [2] configured file count
    out.payload[2] = storage::LOG_FILE_COUNT;

    // [3] current_file
    out.payload[3] = snap.currentFile;

    // [4..7] total_records: uint32 little-endian
    out.payload[4] = static_cast<uint8_t>(totalRecords);
    out.payload[5] = static_cast<uint8_t>(totalRecords >> 8);
    out.payload[6] = static_cast<uint8_t>(totalRecords >> 16);
    out.payload[7] = static_cast<uint8_t>(totalRecords >> 24);

    // [8..11] wrap_count: uint32 little-endian
    out.payload[8] = static_cast<uint8_t>(wrapCount);
    out.payload[9] = static_cast<uint8_t>(wrapCount >> 8);
    out.payload[10] = static_cast<uint8_t>(wrapCount >> 16);
    out.payload[11] = static_cast<uint8_t>(wrapCount >> 24);

    // [12..13] records_in_file / write index: uint16 little-endian
    out.payload[12] = static_cast<uint8_t>(writeIndex);
    out.payload[13] = static_cast<uint8_t>(writeIndex >> 8);


    // out.payload[14] = m_circularLog->lastMountFResult();
    // out.payload[15] = static_cast<uint8_t>(m_circularLog->mountAttempts());

    m_link->send(out);
}

void CommandHandler::dispatch(const protocol::Frame& f)
{
	if (m_stateMachine == nullptr) {
		return;
	}

	switch (f.type) {
		case protocol::FrameType::CmdStart:
			if (!m_stateMachine->isIdle()) {
				sendNack(protocol::NackCode::InvalidState);
				return;
			}

			m_stateMachine->process(Event::UartStart);
			sendStatus();
			sendAck();
			return;

		case protocol::FrameType::CmdStop:
		    if (!m_stateMachine->isLogging()) {
		        sendNack(protocol::NackCode::InvalidState);
		        return;
		    }

		    if (m_circularLog == nullptr) {
		        sendNack(protocol::NackCode::StorageError);
		        return;
		    }

		    if (m_circularLog->flushMeta() != storage::StorageStatus::Ok) {
		        sendNack(protocol::NackCode::StorageError);
		        return;
		    }

		    m_stateMachine->process(Event::UartStop);
		    sendStatus();
		    sendAck();
		    return;

		case protocol::FrameType::CmdStatus:
			sendStatus();
			return;

		case protocol::FrameType::CmdReplay: {
			if (!m_stateMachine->isIdle()) {
				sendNack(protocol::NackCode::InvalidState);
				return;
			}

			if (m_circularLog == nullptr || !m_circularLog->isMounted()) {
				sendNack(protocol::NackCode::StorageError);
				return;
			}

			uint16_t count = 120;

			if (f.len >= 2) {
				count = static_cast<uint16_t>(f.payload[0]) | (static_cast<uint16_t>(f.payload[1]) << 8);
			}

			storage::StorageStatus status = m_circularLog->replayNewest(count, &CommandHandler::replayRecordCallback, this);

			if (status != storage::StorageStatus::Ok && status != storage::StorageStatus::Corrupt) {
				sendNack(protocol::NackCode::StorageError);
				return;
			}

			sendAck();
			return;
		}
		case protocol::FrameType::CmdErase: {
			if (!m_stateMachine->isIdle()) {
				sendNack(protocol::NackCode::InvalidState);
		        return;
			}

			if (m_circularLog == nullptr) {
				sendNack(protocol::NackCode::StorageError);
				return;
			}

			if (f.len < 4) {
				sendNack(protocol::NackCode::BadMagic);
				return;
			}

			const uint32_t magic =
				static_cast<uint32_t>(f.payload[0]) |
		        (static_cast<uint32_t>(f.payload[1]) << 8) |
		        (static_cast<uint32_t>(f.payload[2]) << 16) |
		        (static_cast<uint32_t>(f.payload[3]) << 24);

		    const storage::StorageStatus status = m_circularLog->eraseAll(magic);

		    if (status == storage::StorageStatus::BadMagic) {
		    	sendNack(protocol::NackCode::BadMagic);
		        return;
		    }

		    if (status != storage::StorageStatus::Ok) {
		    	sendNack(protocol::NackCode::StorageError);
		    	return;
		    }

		    sendAck();
		    return;

		}
		default:
			sendNack(protocol::NackCode::BadCommand);
			return;
	}

}

void CommandHandler::sendRecord(const storage::SensorRecord& record)
{
    if (m_link == nullptr) {
        return;
    }

    protocol::Frame out{};
    out.type = protocol::FrameType::Record;
    out.len = sizeof(storage::SensorRecord);

    std::memcpy(out.payload, &record, sizeof(storage::SensorRecord));

    m_link->send(out);
}

bool CommandHandler::replayRecordCallback(const storage::SensorRecord& record, void* ctx)
{
    if (ctx == nullptr) {
        return false;
    }

    auto* handler = static_cast<CommandHandler*>(ctx);
    handler->sendRecord(record);

    return true;
}

} // namespace kern::recorder



#endif /* COMMAND_HANDLER_CPP_ */
