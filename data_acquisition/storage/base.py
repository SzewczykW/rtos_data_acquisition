"""Base class for storage backends."""

from __future__ import annotations

from abc import ABC, abstractmethod


class StorageBackend(ABC):
    """Abstract base class for storage backends."""

    @abstractmethod
    def store(self, channel: int, samples: list[int], timestamp: float) -> None:
        """
        Store ADC samples.

        Args:
            channel: ADC channel number
            samples: List of 12-bit ADC values
            timestamp: Unix timestamp when samples were received
        """
