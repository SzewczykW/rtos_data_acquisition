"""Data acquisition UDP client."""

from __future__ import annotations

import logging
import socket
import time
from dataclasses import dataclass, field
from ipaddress import IPv4Address

from data_acquisition.protocol import (
    HEADER_SIZE,
    Command,
    ConfigParam,
    DataPayload,
    Header,
    LogLevel,
    MsgType,
    ProtocolBuilder,
    StatusPayload,
)

logger = logging.getLogger(__name__)


@dataclass
class Statistics:
    """Session statistics.

    Attributes:
        packets_received (int): Number of data packets received
        samples_received (int): Number of samples received
        bytes_received (int): Number of bytes received
        start_time (float): Timestamp when acquisition started

    Methods:
        print_summary: Print statistics summary
    """

    packets_received: int = 0
    samples_received: int = 0
    bytes_received: int = 0
    start_time: float = field(default_factory=time.time)

    def print_summary(self) -> None:
        """Print statistics summary.

        Calculates elapsed time and sample rate.

        Returns: None
        """
        elapsed = time.time() - self.start_time
        rate = self.samples_received / elapsed if elapsed > 0 else 0

        logger.info("=" * 60)
        logger.info("Session Statistics")
        logger.info("=" * 60)
        logger.info(f"Duration:         {elapsed:.2f} s")
        logger.info(f"Packets received: {self.packets_received}")
        logger.info(f"Samples received: {self.samples_received}")
        logger.info(f"Bytes received:   {self.bytes_received}")
        logger.info(f"Sample rate:      {rate:.1f} samples/s")
        logger.info("=" * 60)


class DataAcquisitionClient:
    """UDP client for LPC1768 data acquisition system.

    Attributes:
        host (str): Target device IP address
        port (int): Target UDP port
        verbose (bool): Enable verbose logging of received data decoding
        stats (Statistics): Session statistics
    """

    def __init__(
        self,
        host: str,
        port: int,
    ):
        """Initialize the client.

        Args:
            host: Target device IP address
            port: Target UDP port
        """
        _ = IPv4Address(host)  # Validate IP address
        self.host = host
        self.port = port

        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        tmp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            logger.debug("Determining local IP address...")
            tmp_socket.connect((self.host, self.port))
            auto_ip, _ = tmp_socket.getsockname()
            self.local_ip = auto_ip
            logger.debug("Local IP address determined: %s", self.local_ip)
        finally:
            tmp_socket.close()

        self._sock.bind((self.local_ip, 0))
        self._sock.settimeout(1.0)

        self._builder = ProtocolBuilder()
        self.stats = Statistics()
        self.running = False

        logger.info(
            "Client bound to port %d at %s",
            self._sock.getsockname()[1],
            self._sock.getsockname()[0],
        )
        logger.info("Target: %s:%d", host, port)

    def send(self, data: bytes) -> None:
        """Send raw data to target device.

        Args:
            data (bytes): Bytes to send

        Returns: None
        """
        self._sock.sendto(data, (self.host, self.port))
        logger.debug("Sent %d bytes to %s:%d", len(data), self.host, self.port)

    def send_command(self, cmd: Command, param_type: int = 0, param: int = 0) -> None:
        """Send a command packet.

        Args:
            cmd (Command): Command to send
            param_type (int): Parameter type
            param (int): Parameter value

        Returns: None
        """
        packet = self._builder.build_command(cmd, param_type, param)
        self.send(packet)
        logger.debug(
            "Sent command: %s (param_type=%d, param=%d)", cmd.name, param_type, param
        )

    def start_acquisition(self) -> None:
        """Start data acquisition on the device.

        Return: None
        """
        self.send_command(Command.START_ACQ)
        logger.info("Sent START_ACQ command")

    def stop_acquisition(self) -> None:
        """Stop data acquisition on the device.

        Return: None
        """
        self.send_command(Command.STOP_ACQ)
        logger.info("Sent STOP_ACQ command")

    def get_status(self) -> StatusPayload | None:
        """Request device status.

        Return: StatusPayload or None
        """
        self.send_command(Command.GET_STATUS)
        logger.debug("Sent GET_STATUS command")

        try:
            data, _ = self._sock.recvfrom(2048)
            header = Header.unpack(data)

            if not header.is_valid():
                logger.warning("Invalid magic in response")
                return None

            if header.msg_type == MsgType.STATUS:
                return StatusPayload.unpack(data[HEADER_SIZE:])

        except TimeoutError:
            logger.warning("Status request timed out")

        return None

    def configure_threshold_percent(self, percent: int) -> None:
        """Set threshold as percentage.

        Args:
            percent (int): Threshold percentage

        Returns: None
        """
        if not (0 <= percent <= 100):
            raise ValueError("Threshold percentage must be between 0 and 100")
        self.send_command(Command.CONFIGURE, ConfigParam.THRESHOLD_PERCENT, percent)
        time.sleep(0.1)  # Allow device to process command
        logger.info("Configured threshold: %d%%", percent)

    def configure_threshold_mv(self, mv: int) -> None:
        """Set threshold in millivolts.

        Args:
            mv (int): Threshold in millivolts (0-3300)

        Returns: None
        """
        if not (0 <= mv <= 3300):
            raise ValueError("Threshold in millivolts must be between 0 and 3300")
        self.send_command(Command.CONFIGURE, ConfigParam.THRESHOLD_MV, mv)
        time.sleep(0.1)  # Allow device to process command
        logger.info("Configured threshold: %d mV", mv)

    def configure_batch_size(self, size: int) -> None:
        """Set batch size - samples per packet.

        Args:
            size (int): Batch size (1-100)

        Returns: None
        """
        if not (1 <= size <= 100):
            raise ValueError("Batch size must be between 1 and 100")
        self.send_command(Command.CONFIGURE, ConfigParam.BATCH_SIZE, size)
        time.sleep(0.1)  # Allow device to process command
        logger.info("Configured batch size: %d", size)

    def configure_channel(self, channel: int) -> None:
        """Set ADC channel.

        Args:
            channel (int): ADC channel (0-7)

        Returns: None
        """
        if not (0 <= channel <= 7):
            raise ValueError("ADC channel must be between 0 and 7")
        self.send_command(Command.CONFIGURE, ConfigParam.CHANNEL, channel)
        time.sleep(0.1)  # Allow device to process command
        logger.info("Configured channel: %d", channel)

    def reset_sequence(self) -> None:
        """Reset sequence counter on device.

        Return: None
        """
        self.send_command(Command.CONFIGURE, ConfigParam.RESET_SEQUENCE, 0)
        time.sleep(0.1)  # Allow device to process command
        logger.info("Reset sequence counter")

    def set_log_level(self, level: int) -> None:
        """Set device log level

        Args:
            level (int): Log level

        Returns: None
        """
        if not any(level == lvl.value for lvl in LogLevel):
            raise ValueError("Invalid log level")
        self.send_command(Command.CONFIGURE, ConfigParam.LOG_LEVEL, level)
        time.sleep(0.1)  # Allow device to process command
        logger.info("Set device log level: %d", int(level))

    def ping(self) -> float | None:
        """Send ping and measure round-trip time.

        Return:
            float: Round-trip time in milliseconds, or None on timeout
        """
        packet = self._builder.build_ping()
        start = time.perf_counter()
        self.send(packet)

        try:
            data, _ = self._sock.recvfrom(2048)
            rtt = (time.perf_counter() - start) * 1000

            header = Header.unpack(data)
            if header.is_valid() and header.msg_type == MsgType.PONG:
                return rtt

        except TimeoutError:
            pass

        return None

    def _handle_data_packet(self, data: bytes) -> None:
        """Process received data packet and log it to stdout.

        Args:
            data (bytes): Raw packet data

        Returns: None
        """
        header = Header.unpack(data)
        payload = DataPayload.unpack(data[HEADER_SIZE:])

        self.stats.packets_received += 1
        self.stats.samples_received += len(payload.samples)
        self.stats.bytes_received += len(data)

        ts = time.time()
        if payload.samples:
            line = f"{ts:.6f},{header.sequence},{payload.channel}," + ",".join(
                str(s) for s in payload.samples
            )
        else:
            line = f"{ts:.6f},{header.sequence},{payload.channel}"

        logger.debug(line)

        logger.info(
            "[%5d] CH%d: %d samples",
            header.sequence,
            payload.channel,
            len(payload.samples),
        )

    def receive_loop(
        self,
        *,
        duration_s: float | None = None,
        max_samples: int | None = None,
    ) -> None:
        """Main receive loop.

        Stops when either:
        - stop() is called.
        - duration_s elapses.
        - max_samples is reached.

        Args:
            duration_s (float | None): Maximum duration in seconds
            max_samples (int | None): Maximum number of samples to receive

        Returns: None
        """
        self.running = True
        start_t = time.monotonic()
        deadline = (start_t + duration_s) if duration_s is not None else None

        if duration_s is not None:
            logger.info("Starting receive loop (duration: %.3f s)", duration_s)
        elif max_samples is not None:
            logger.info("Starting receive loop (samples: %d)", max_samples)
        else:
            logger.info("Starting receive loop")

        while self.running:
            try:
                if deadline is not None and time.monotonic() >= deadline:
                    logger.info("Reached duration limit, stopping receive loop")
                    break

                if (
                    max_samples is not None
                    and self.stats.samples_received >= max_samples
                ):
                    logger.info("Reached sample limit, stopping receive loop")
                    break

                data, addr = self._sock.recvfrom(2048)

                if len(data) < HEADER_SIZE:
                    continue

                header = Header.unpack(data)

                if not header.is_valid():
                    logger.warning("Invalid magic from %s", addr)
                    continue

                if header.msg_type == MsgType.DATA:
                    self._handle_data_packet(data)

                elif header.msg_type == MsgType.PONG:
                    logger.debug("Received PONG")

                elif header.msg_type == MsgType.STATUS:
                    status = StatusPayload.unpack(data[HEADER_SIZE:])
                    logger.info(
                        "Status: acquiring=%s, ch=%d, thresh=%dmV, uptime=%ds, samples=%d",
                        status.acquiring,
                        status.channel,
                        status.threshold_mv,
                        status.uptime,
                        status.samples_sent,
                    )

            except TimeoutError:
                continue
            except Exception as e:
                logger.error("Error in receive loop: %s", e)

        self.running = False

    def stop(self) -> None:
        """Signal the receive loop to stop.

        Returns: None
        """
        self.running = False

    def close(self) -> None:
        """Close client and print statistics summary.

        Returns: None
        """
        self._sock.close()
        self.stats.print_summary()
