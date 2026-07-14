#include <iostream>
#include <cassert>
#include <cstring>

// Include the logical headers we are testing
#include "../../firmware/recorder/state_machine.hpp"
#include "../../firmware/recorder/command_handler.hpp"
#include "../../firmware/protocol/frame.hpp"
#include "../../firmware/storage/circular_log.hpp"
#include "../../firmware/recorder/comm_link.hpp"

using namespace kern::recorder;
using namespace kern::protocol;
using namespace kern::storage;

// LINKER MOCKS (Aligned exactly with tests/host/ff.h)

// 1. Mocking FatFs (C functions) using the exact signatures from ff.h
extern "C" {
    FRESULT f_mount(FATFS* fs, const char* path, uint8_t opt) {
        (void)fs; (void)path; (void)opt;
        return FR_OK;
    }
    FRESULT f_open(FIL* fp, const char* path, uint8_t mode) {
        (void)fp; (void)path; (void)mode;
        return FR_OK;
    }
    FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br) {
        (void)fp; (void)buff; (void)btr; (void)br;
        return FR_OK;
    }
    FRESULT f_close(FIL* fp) {
        (void)fp;
        return FR_OK;
    }
    FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw) {
        (void)fp; (void)buff; (void)btw; (void)bw;
        return FR_OK;
    }
    FRESULT f_sync(FIL* fp) {
        (void)fp;
        return FR_OK;
    }
    FRESULT f_lseek(FIL* fp, uint32_t ofs) {
        (void)fp; (void)ofs;
        return FR_OK;
    }
    FRESULT f_unlink(const char* path) {
        (void)path;
        return FR_OK;
    }
}

// 2. Mocking CommLink (C++ methods)
namespace kern::recorder {
    Frame g_last_sent_frame{};

    CommLink::CommLink(UART_HandleTypeDef*) {}

    void CommLink::send(const Frame& f) {
        g_last_sent_frame = f;
    }
}

// TEST CASES

void test_fsm_basic_transitions() {
    StateMachine sm;

    assert(sm.state() == State::Idle);
    sm.process(Event::ChecksPassed);
    assert(sm.state() == State::Idle);

    sm.process(Event::UartStart);
    assert(sm.state() == State::Recording);

    sm.process(Event::UartStop);
    assert(sm.state() == State::Idle);

    sm.process(Event::UartStart);
    sm.process(Event::ShortPress);
    assert(sm.state() == State::Idle);

    sm.process(Event::SdFault);
    assert(sm.state() == State::Fault);

    sm.process(Event::FaultCleared);
    assert(sm.state() == State::Recording);

    sm.process(Event::SdFault);
    assert(sm.state() == State::Fault);

    std::cout << "test_fsm_basic_transitions: PASSED" << std::endl;
}

void test_orchestrator_write_policy() {
    StateMachine sm;
    sm.process(Event::UartStart);

    int consecutive_fails = 0;

    consecutive_fails++;
    assert(sm.state() == State::Recording);

    consecutive_fails++;
    assert(sm.state() == State::Recording);

    consecutive_fails++;
    if (consecutive_fails == 3) {
        sm.process(Event::SdFault);
    }
    assert(sm.state() == State::Fault);

    std::cout << "test_orchestrator_write_policy: PASSED" << std::endl;
}

void test_command_handler_guards() {
    g_last_sent_frame = {};

    CommLink link(nullptr);
    StateMachine sm;
    CircularLog log;
    CommandHandler handler;

    handler.init(&link, &sm, &log);

    Frame cmd{};

    // --- TEST: CMD_STOP from Idle -> NACK InvalidState ---
    assert(sm.isIdle());
    cmd.type = FrameType::CmdStop;
    handler.dispatch(cmd);
    assert(g_last_sent_frame.type == FrameType::Nack);
    assert(g_last_sent_frame.payload[0] == static_cast<uint8_t>(NackCode::InvalidState));

    // --- TEST: CMD_ERASE from Recording -> NACK InvalidState ---
    sm.process(Event::UartStart);
    assert(sm.isLogging());
    cmd.type = FrameType::CmdErase;
    handler.dispatch(cmd);
    assert(g_last_sent_frame.type == FrameType::Nack);
    assert(g_last_sent_frame.payload[0] == static_cast<uint8_t>(NackCode::InvalidState));

    // --- TEST: CMD_START from Recording -> NACK InvalidState ---
    cmd.type = FrameType::CmdStart;
    handler.dispatch(cmd);
    assert(g_last_sent_frame.type == FrameType::Nack);
    assert(g_last_sent_frame.payload[0] == static_cast<uint8_t>(NackCode::InvalidState));

    // --- TEST: CMD_REPLAY from Fault -> NACK InvalidState ---
    sm.process(Event::SdFault);
    assert(sm.isFault());
    cmd.type = FrameType::CmdReplay;
    handler.dispatch(cmd);
    assert(g_last_sent_frame.type == FrameType::Nack);
    assert(g_last_sent_frame.payload[0] == static_cast<uint8_t>(NackCode::InvalidState));

    std::cout << "test_command_handler_guards: PASSED" << std::endl;
}

// MAIN RUNNER
int main() {
    std::cout << "=== RUNNING HOST TESTS FOR FSM & COMMANDS ===" << std::endl;

    test_fsm_basic_transitions();
    test_orchestrator_write_policy();
    test_command_handler_guards();

    std::cout << "=== ALL TESTS PASSED! ===" << std::endl;
    return 0;
}
