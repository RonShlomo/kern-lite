import struct
from groundstation.frame import Frame, FrameType
import threading
import time
import logging


class CommandSender:
    def send_start(self, link):
        link.send_frame(Frame(type=FrameType.CmdStart))

    def send_stop(self, link):
        link.send_frame(Frame(type=FrameType.CmdStop))

    def send_status(self, link):
        link.send_frame(Frame(type=FrameType.CmdStatus))

    def send_replay(self, link, n: int):
        link.send_frame(Frame(type=FrameType.CmdReplay,
                              payload=struct.pack("<H", n)))

    def send_erase(self, link, magic: int):
        link.send_frame(Frame(type=FrameType.CmdErase,
                              payload=struct.pack("<I", magic)))


# Set up logging to show warnings and info in the terminal
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("StatusPoller")

class StatusPoller:
    def __init__(self, link, command_sender, poll_interval_s=5.0):
        # initializes the poller with necessary dependencies
        self.link = link
        self.command_sender = command_sender
        self.poll_interval_s = poll_interval_s

        # Keep track of the last known record count to detect reboots
        self.last_total_records = None

        # thread control flags
        self.is_running = False
        self.thread = None

    def start(self):
        # Starts the background thread, non blocking.
        if self.is_running:
            # already running, do nothing
            return

        self.is_running = True
        # daemon=True ensures this thread dies automatically when the main program closes
        self.thread = threading.Thread(target=self._poll_loop, daemon=True)
        self.thread.start()
        logger.info("Status poller thread started.")


    def stop(self):
        # Stops the background thread safely
        self.is_running = False
        if self.thread:
            # wait for the thread to finish its current loop
            self.thread.join(timeout=1.0)
            logger.info("Status poller thread stopped.")

    def _poll_loop(self):
        # The actual loop running inside the background thread
        while (self.is_running) :
            try:
                # check if the hardware link is actually connected before sending
                if hasattr(self.link, "connection_state") and self.link.connection_state == "connected":
                    self.command_sender.send_status(self.link)
            except Exception as e:
                logger.error(f"Failed to send continuous status poll: {e}")

            # sleep for 5 sec without freezing the main app
            time.sleep(self.poll_interval_s)

    def process_incoming_status(self, current_total_records):
        """
        This method must be called whenever the link receives a valid STATUS frame response.
        It handles the business logic for detecting device reboots.
        """

        # if it's the first time receiving a status just save it and move on
        if self.last_total_records is None:
            self.last_total_records = current_total_records
            return

        # Reboot detection logic:
        # If the new count is suddenly much smaller than what we saw before,
        # the board must have restarted (resetting its volatile counters to 0).
        if current_total_records < self.last_total_records:
            logger.warning("REBOOT_DETECTED EVENT. Board layout cleared or reset unexpectedly.")
            # Trigger a manual immediate status poll to sync up immediately
            try:
                self.command_sender.send_status(self.link)
            except Exception as e:
                logger.error(f"Failed to re-issue status after reboot detection: {e}")

        # Update the memory state with the newest value for the next comparison
        self.last_total_records = current_total_records
