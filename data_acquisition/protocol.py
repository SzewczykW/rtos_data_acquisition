"""
Protocol definitions for LPC1768 data acquisition system.

This module contains all protocol constants, message types, and packet
structures that must match the C implementation in protocol.h.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from enum import IntEnum

PROTOCOL_MAGIC = 0xDA7A
HEADER_SIZE = 7


class MsgType(IntEnum):
    """Protocol message types."""

    PING = 0x01
    PONG = 0x02
    DATA = 0x10
    CMD = 0x20
    STATUS = 0x30


class Command(IntEnum):
    """Command codes."""

    START_ACQ = 0x01
    STOP_ACQ = 0x02
    GET_STATUS = 0x03
    CONFIGURE = 0x04


class ConfigParam(IntEnum):
    """Configuration parameter types for CMD_CONFIGURE."""

    THRESHOLD_PERCENT = 0
    THRESHOLD_MV = 1
    BATCH_SIZE = 2
    CHANNEL = 3
    RESET_SEQUENCE = 4
    LOG_LEVEL = 5


class LogLevel(IntEnum):
    """Device log levels."""

    DEBUG = 0
    INFO = 1
    WARNING = 2
    ERROR = 3
    CRITICAL = 4
    NONE = 5


@dataclass
class Header:
    """
    Protocol header (7 bytes).

    Format (little-endian):
        +-----------------+---------------+-----------------+------------------+
        |      0xDA7A     | MSG_TYPE (1B) | SEQUENCE (2B)   | PAYLOAD_LEN (2B) |
        +-----------------+---------------+-----------------+------------------+

    Attributes:
        magic: Magic number (0xDA7A)
        msg_type: Message type
        sequence: Packet sequence number
        payload_len: Length of payload in bytes
    """

    magic: int
    msg_type: int
    sequence: int
    payload_len: int

    FORMAT = "<HBHH"

    @classmethod
    def unpack(cls, data: bytes) -> Header:
        """Unpack header from bytes.

        Args:
            data (bytes): Raw bytes containing the header
        Returns:
            Header: Unpacked header object
        """
        magic, msg_type, seq, payload_len = struct.unpack(
            cls.FORMAT, data[:HEADER_SIZE]
        )
        return cls(magic, msg_type, seq, payload_len)

    def pack(self) -> bytes:
        """Pack header to bytes.

        Returns:
            bytes: Packed header bytes
        """
        return struct.pack(
            self.FORMAT, self.magic, self.msg_type, self.sequence, self.payload_len
        )

    def is_valid(self) -> bool:
        """Check if header has valid magic number.

        Returns:
            bool: True if magic number is valid, False otherwise
        """
        return self.magic == PROTOCOL_MAGIC


@dataclass
class DataPayload:
    """
    ADC data payload (UNDEFINED size - depends on sample count).

    Format (little-endian):
        +-------------+-------------+-----------------+-----------------+
        |CHANNEL (1B) |RESERVED (1B)|SAMPLE_CNT (2B)  | samples[]...    |
        +-------------+-------------+-----------------+-----------------+

    Attributes:
        channel: ADC channel number (0-7)
        samples: List of acquired samples (16-bit unsigned integers)
    """

    channel: int
    samples: list[int] = field(default_factory=list)

    @classmethod
    def unpack(cls, data: bytes) -> DataPayload:
        """Unpack data payload from bytes.

        Args:
            data (bytes): Raw bytes containing the data payload

        Returns:
            DataPayload: Unpacked data payload object
        """
        channel, _, sample_count = struct.unpack("<BBH", data[:4])
        samples = list(
            struct.unpack(f"<{sample_count}H", data[4 : 4 + sample_count * 2])
        )
        return cls(channel, samples)


@dataclass
class StatusPayload:
    """
    Status response payload (12 bytes).

    Format (little-endian):
        +------------+------------+-----------------+-----------------+
        |  ACQ (1B)  |   CH (1B)  | THRESH_MV (2B)  |   UPTIME (4B)   |
        +------------+------------+-----------------+-----------------+
        |      SAMPLES_SENT (4B)            |
        +--------+--------+--------+--------+

    Attributes:
        acquiring: Whether acquisition is currently active
        channel: Configured ADC channel
        threshold_mv: Configured threshold in millivolts
        uptime: Device uptime in seconds
        samples_sent: Total number of samples sent to host
    """

    acquiring: bool
    channel: int
    threshold_mv: int
    uptime: int
    samples_sent: int

    FORMAT = "<BBHII"

    @classmethod
    def unpack(cls, data: bytes) -> StatusPayload:
        """Unpack status payload from bytes.

        Args:
            data (bytes): Raw bytes containing the status payload

        Returns:
            StatusPayload: Unpacked status payload object
        """
        acq, ch, thresh, uptime, samples = struct.unpack(cls.FORMAT, data[:12])
        return cls(bool(acq), ch, thresh, uptime, samples)


class ProtocolBuilder:
    """Builds protocol packets with automatic sequence numbering."""

    def __init__(self) -> None:
        self._sequence = 0

    def _next_seq(self) -> int:
        """Get next sequence number

        Returns:
            int: Next sequence number
        """
        seq = self._sequence
        self._sequence = (self._sequence + 1) & 0xFFFF
        return seq

    def build_command(self, cmd: Command, param_type: int = 0, param: int = 0) -> bytes:
        """
        Build a command packet.

        Args:
            cmd (Command): Command code
            param_type (int): Parameter type (for CMD_CONFIGURE)
            param (int): Parameter value

        Returns:
            bytes: Complete packet bytes
        """
        payload = struct.pack("<BBH", cmd, param_type, param)
        header = Header(
            magic=PROTOCOL_MAGIC,
            msg_type=MsgType.CMD,
            sequence=self._next_seq(),
            payload_len=len(payload),
        )
        return header.pack() + payload

    def build_ping(self) -> bytes:
        """Build a ping packet.

        Returns:
            bytes: Complete ping packet bytes
        """
        header = Header(
            magic=PROTOCOL_MAGIC,
            msg_type=MsgType.PING,
            sequence=self._next_seq(),
            payload_len=0,
        )
        return header.pack()
