"""Null storage backend that discards all data."""

from __future__ import annotations

from data_acquisition.storage.base import StorageBackend


class NullStorage(StorageBackend):
    """
    Dummy storage that discards all data.

    Use this when you don't want to save data to disk (default mode).
    """

    def store(self, channel: int, samples: list[int], timestamp: float) -> None:
        """Discard samples."""
        pass
