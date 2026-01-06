"""SQLite database storage backend."""

from __future__ import annotations

import logging
import sqlite3
import time
from pathlib import Path

from data_acquisition.storage.base import StorageBackend

logger = logging.getLogger(__name__)


class SqliteStorage(StorageBackend):
    """
    SQLite database storage backend.

    Schema:
        sessions:
            - id: INTEGER PRIMARY KEY
            - start_time: REAL (Unix timestamp)
            - end_time: REAL (Unix timestamp, NULL until closed)

        samples:
            - id: INTEGER PRIMARY KEY
            - session_id: INTEGER (FK -> sessions.id)
            - timestamp: REAL (Unix timestamp)
            - channel: INTEGER (ADC channel)
            - value: INTEGER (12-bit ADC value)

    Samples are batched for efficient bulk insertion.
    """

    BATCH_SIZE = 1000

    def __init__(self, filepath: Path) -> None:
        """
        Initialize SQLite storage.

        Args:
            filepath: Path to the database file
        """
        self.filepath = filepath
        self._conn = sqlite3.connect(str(filepath))
        self._batch: list[tuple] = []
        self._session_id: int = 0

        self._create_schema()
        self._start_session()

        logger.info("Opened SQLite database: %s", filepath)

    def _create_schema(self) -> None:
        """Create database tables and indexes."""
        cursor = self._conn.cursor()

        cursor.execute("""
            CREATE TABLE IF NOT EXISTS sessions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                start_time REAL NOT NULL,
                end_time REAL
            )
        """)

        cursor.execute("""
            CREATE TABLE IF NOT EXISTS samples (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id INTEGER NOT NULL,
                timestamp REAL NOT NULL,
                channel INTEGER NOT NULL,
                value INTEGER NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(id)
            )
        """)

        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_samples_session
            ON samples(session_id)
        """)

        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_samples_timestamp
            ON samples(timestamp)
        """)

        self._conn.commit()

    def _start_session(self) -> None:
        """Create a new acquisition session."""
        cursor = self._conn.cursor()
        cursor.execute("INSERT INTO sessions (start_time) VALUES (?)", (time.time(),))
        self._session_id = cursor.lastrowid or 0
        self._conn.commit()
        logger.debug("Started session %d", self._session_id)

    def store(self, channel: int, samples: list[int], timestamp: float) -> None:
        """Add samples to the batch, flushing if needed."""
        for sample in samples:
            self._batch.append((self._session_id, timestamp, channel, sample))

        if len(self._batch) >= self.BATCH_SIZE:
            self._flush()

    def _flush(self) -> None:
        """Write pending samples to database."""
        if not self._batch:
            return

        cursor = self._conn.cursor()
        cursor.executemany(
            "INSERT INTO samples (session_id, timestamp, channel, value) "
            "VALUES (?, ?, ?, ?)",
            self._batch,
        )
        self._conn.commit()

        logger.debug("Flushed %d samples to database", len(self._batch))
        self._batch.clear()

    def close(self) -> None:
        """Flush pending data, close session, and close database."""
        self._flush()

        cursor = self._conn.cursor()
        cursor.execute(
            "UPDATE sessions SET end_time = ? WHERE id = ?",
            (time.time(), self._session_id),
        )
        self._conn.commit()
        self._conn.close()

        logger.info("Closed SQLite database: %s", self.filepath)
