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
HEADER_SIZE = 7  # magic(2) + msg_type(1) + sequence(2) + payload_len(2)


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
    """
    Configuration parameter types for CMD_CONFIGURE.

    These values must match protocol_config_param_t in protocol.h.
    """

    THRESHOLD_PERCENT = 0  # Threshold as percentage (0-100)
    THRESHOLD_MV = 1  # Threshold in millivolts (0-3300)
    BATCH_SIZE = 2  # Samples per packet (1-500)
    CHANNEL = 3  # ADC channel (0-7)
    RESET_SEQUENCE = 4  # Reset sequence counter (param ignored)
    LOG_LEVEL = 5  # Set log level (0=DEBUG..5=NONE)


class LogLevel(IntEnum):
    """
    Device log levels.

    These values must match log_level_t in logger.h.
    """

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

    Wire format (little-endian):
        +--------+--------+--------+--------+--------+--------+--------+
        |    MAGIC (2B)   |MSG_TYPE| SEQUENCE (2B)   |PAYLOAD_LEN (2B) |
        +--------+--------+--------+--------+--------+--------+--------+
    """

    magic: int
    msg_type: int
    sequence: int
    payload_len: int

    FORMAT = "<HBHH"

    @classmethod
    def unpack(cls, data: bytes) -> Header:
        """Unpack header from bytes."""
        magic, msg_type, seq, payload_len = struct.unpack(
            cls.FORMAT, data[:HEADER_SIZE]
        )
        return cls(magic, msg_type, seq, payload_len)

    def pack(self) -> bytes:
        """Pack header to bytes."""
        return struct.pack(
            self.FORMAT, self.magic, self.msg_type, self.sequence, self.payload_len
        )

    def is_valid(self) -> bool:
        """Check if header has valid magic number."""
        return self.magic == PROTOCOL_MAGIC


@dataclass
class DataPayload:
    """
    ADC data payload.

    Wire format (little-endian):
        +--------+--------+--------+--------+--------+--------+
        |CHANNEL |RESERVED|SAMPLE_CNT (2B)  | samples[]...    |
        +--------+--------+--------+--------+--------+--------+
    """

    channel: int
    samples: list[int] = field(default_factory=list)

    @classmethod
    def unpack(cls, data: bytes) -> DataPayload:
        """Unpack data payload from bytes."""
        channel, _, sample_count = struct.unpack("<BBH", data[:4])
        samples = list(
            struct.unpack(f"<{sample_count}H", data[4 : 4 + sample_count * 2])
        )
        return cls(channel, samples)


@dataclass
class StatusPayload:
    """
    Status response payload (12 bytes).

    Wire format (little-endian):
        +--------+--------+--------+--------+--------+--------+
        |  ACQ   |   CH   | THRESH_MV (2B)  |   UPTIME (4B)   |
        +--------+--------+--------+--------+--------+--------+
        |      SAMPLES_SENT (4B)            |
        +--------+--------+--------+--------+
    """

    acquiring: bool
    channel: int
    threshold_mv: int
    uptime: int
    samples_sent: int

    FORMAT = "<BBHII"

    @classmethod
    def unpack(cls, data: bytes) -> StatusPayload:
        """Unpack status payload from bytes."""
        acq, ch, thresh, uptime, samples = struct.unpack(cls.FORMAT, data[:12])
        return cls(bool(acq), ch, thresh, uptime, samples)


class ProtocolBuilder:
    """Builds protocol packets with automatic sequence numbering."""

    def __init__(self) -> None:
        self._sequence = 0

    def _next_seq(self) -> int:
        """Get next sequence number (wraps at 16 bits)."""
        seq = self._sequence
        self._sequence = (self._sequence + 1) & 0xFFFF
        return seq

    def build_command(self, cmd: Command, param_type: int = 0, param: int = 0) -> bytes:
        """
        Build a command packet.

        Args:
            cmd: Command code
            param_type: Parameter type (for CMD_CONFIGURE)
            param: Parameter value

        Returns:
            Complete packet bytes
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
        """Build a ping packet."""
        header = Header(
            magic=PROTOCOL_MAGIC,
            msg_type=MsgType.PING,
            sequence=self._next_seq(),
            payload_len=0,
        )
        return header.pack()
