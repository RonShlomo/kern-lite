CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I../../firmware -I.

FW_CRC = ../../firmware/protocol/crc32.cpp
FW_CODEC = ../../firmware/protocol/codec.cpp
FW_STORAGE = ../../firmware/storage/circular_log.cpp
FW_STATE_MACHINE = ../../firmware/recorder/state_machine.cpp
FW_COMMAND_HANDLER = ../../firmware/recorder/command_handler.cpp

TARGETS = test_codec test_dsp test_storage test_fsm

all: $(TARGETS)

test_codec: test_codec.cpp $(FW_CRC) $(FW_CODEC)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_dsp: test_dsp.cpp $(FW_CRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_storage: test_storage.cpp $(FW_STORAGE) $(FW_CRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

# test_fsm.cpp mocks CommLink::CommLink/CommLink::send itself, so comm_link.cpp is deliberately
# not linked here -- that would duplicate those symbols.
test_fsm: test_fsm.cpp $(FW_STATE_MACHINE) $(FW_COMMAND_HANDLER) $(FW_STORAGE) $(FW_CRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS) *.exe