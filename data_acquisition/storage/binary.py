"""Binary file storage backend."""

from __future__ import annotations

import logging
import struct
from pathlib import Path

from data_acquisition.storage.base import StorageBackend

logger = logging.getLogger(__name__)


class BinaryStorage(StorageBackend):
    """
    Binary file storage backend.

    File format (per record):
        +----------+--------+-------+-------------------+
        | timestamp| channel| count |     samples[]     |
        |  (8B)    |  (1B)  | (2B)  |   (count * 2B)    |
        +----------+--------+-------+-------------------+

    All values are little-endian.
    - timestamp: float64 (Unix timestamp)
    - channel: uint8
    - count: uint16
    - samples: array of uint16
    """

    def __init__(self, filepath: Path) -> None:
        """
        Initialize binary storage.

        Args:
            filepath: Path to the output file
        """
        self.filepath = filepath
        logger.info("Opened binary file: %s", filepath)

    def store(self, channel: int, samples: list[int], timestamp: float) -> None:
        """Write samples to binary file."""
        count = len(samples)
        header = struct.pack("<dBH", timestamp, channel, count)
        data = struct.pack(f"<{count}H", *samples)
        with open(self.filepath, "ab") as f:
            f.write(header + data)
