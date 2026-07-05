#ifndef COMMAND_HANDLER_CPP_
#define COMMAND_HANDLER_CPP_

#include "command_handler.hpp"
#include "comm_link.hpp"

namespace kern::recorder {

void CommandHandler::init(CommLink* link)
{
    m_link = link;
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
    if (m_link == nullptr) {
        return;
    }

    protocol::Frame out{};
    out.type = protocol::FrameType::Status;
    out.len = 14;

    // STATUS payload, stub version for Day 2:
    // [0] state: 0 = Idle
    out.payload[0] = 0;

    // [1] sd_mounted: 1 = mounted stub
    out.payload[1] = 1;

    // [2] file_count: 4
    out.payload[2] = 4;

    // [3] current_file: 0
    out.payload[3] = 0;

    // [4..7] total_records: u32 LE = 0
    out.payload[4] = 0;
    out.payload[5] = 0;
    out.payload[6] = 0;
    out.payload[7] = 0;

    // [8..11] wrap_count: u32 LE = 0
    out.payload[8] = 0;
    out.payload[9] = 0;
    out.payload[10] = 0;
    out.payload[11] = 0;

    // [12..13] records_in_file: u16 LE = 0
    out.payload[12] = 0;
    out.payload[13] = 0;

    m_link->send(out);
}

void CommandHandler::dispatch(const protocol::Frame& f)
{
    if (f.type == protocol::FrameType::CmdStatus) {
        sendStatus();
        return;
    }

    sendNack(protocol::NackCode::BadCommand);
}

} // namespace kern::recorder



#endif /* COMMAND_HANDLER_CPP_ */
