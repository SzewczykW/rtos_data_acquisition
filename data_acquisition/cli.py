"""Command-line interface for data acquisition client."""

from __future__ import annotations

import argparse
import logging
import signal
import sys
import time
from datetime import datetime
from pathlib import Path

from data_acquisition.client import DataAcquisitionClient
from data_acquisition.storage.base import StorageBackend
from data_acquisition.storage.binary import BinaryStorage
from data_acquisition.storage.null import NullStorage
from data_acquisition.storage.sqlite import SqliteStorage

logger = logging.getLogger(__name__)


# =============================================================================
# Logging setup
# =============================================================================


def setup_logging(verbose: bool, debug: bool) -> None:
    """Configure logging based on verbosity flags."""
    if debug:
        level = logging.DEBUG
    elif verbose:
        level = logging.INFO
    else:
        level = logging.WARNING

    logging.basicConfig(
        level=level,
        format="%(asctime)s.%(msecs)03d [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )


# =============================================================================
# Storage factory
# =============================================================================


def create_storage(args: argparse.Namespace) -> StorageBackend:
    """Create appropriate storage backend based on CLI arguments."""
    if args.no_save:
        logger.info("Storage disabled (--no-save)")
        return NullStorage()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Explicit file paths take precedence
    if args.sqlite:
        return SqliteStorage(Path(args.sqlite))

    if args.binary:
        return BinaryStorage(Path(args.binary))

    # Auto-generate filename in output directory
    if args.output_dir:
        output_dir = Path(args.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        if args.format == "sqlite":
            return SqliteStorage(output_dir / f"acquisition_{timestamp}.db")
        else:
            return BinaryStorage(output_dir / f"acquisition_{timestamp}.bin")

    # Default: no storage
    return NullStorage()


# =============================================================================
# Command handlers
# =============================================================================


def cmd_start(client: DataAcquisitionClient, args: argparse.Namespace) -> None:
    """Handle 'start' command - configure, start acquisition, receive data."""
    # Validate required arguments
    has_threshold = args.threshold_mv is not None or args.threshold_percent is not None
    has_channel = args.channel is not None

    if not has_threshold or not has_channel:
        missing = []
        if not has_threshold:
            missing.append("--threshold-mv or --threshold-percent")
        if not has_channel:
            missing.append("--channel")
        print(f"Error: Missing required arguments: {', '.join(missing)}")
        sys.exit(1)

    # Apply optional configuration first
    if args.reset_sequence:
        client.reset_sequence()
        time.sleep(0.1)

    if args.log_level is not None:
        client.set_log_level(args.log_level)
        time.sleep(0.1)

    if args.batch_size is not None:
        client.configure_batch_size(args.batch_size)
        time.sleep(0.1)

    # Apply required configuration
    # mV takes precedence over percent if both specified
    if args.threshold_mv is not None:
        client.configure_threshold_mv(args.threshold_mv)
    elif args.threshold_percent is not None:
        client.configure_threshold_percent(args.threshold_percent)
    time.sleep(0.1)

    client.configure_channel(args.channel)
    time.sleep(0.1)

    # Start acquisition
    client.start_acquisition()
    time.sleep(0.1)

    # Receive loop
    try:
        client.receive_loop()
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        client.stop_acquisition()


def cmd_stop(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'stop' command."""
    client.stop_acquisition()
    print("Stop command sent")


def cmd_status(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'status' command."""
    status = client.get_status()

    if status:
        print(f"Acquiring:    {status.acquiring}")
        print(f"Channel:      {status.channel}")
        print(f"Threshold:    {status.threshold_mv} mV")
        print(f"Uptime:       {status.uptime} s")
        print(f"Samples sent: {status.samples_sent}")
    else:
        print("Failed to get status")
        sys.exit(1)


def cmd_ping(client: DataAcquisitionClient, args: argparse.Namespace) -> None:
    """Handle 'ping' command."""
    count = args.count

    for i in range(count):
        rtt = client.ping()

        if rtt is not None:
            print(f"Pong from {client.host}: time={rtt:.2f} ms")
        else:
            print("Request timed out")

        if i < count - 1:
            time.sleep(1)


def cmd_configure(client: DataAcquisitionClient, args: argparse.Namespace) -> None:
    """Handle 'configure' command."""
    configured = False

    # Optional resets first
    if args.reset_sequence:
        client.reset_sequence()
        configured = True

    if args.log_level is not None:
        client.set_log_level(args.log_level)
        configured = True

    # Then configuration
    if args.threshold_percent is not None:
        client.configure_threshold_percent(args.threshold_percent)
        configured = True

    if args.threshold_mv is not None:
        client.configure_threshold_mv(args.threshold_mv)
        configured = True

    if args.batch_size is not None:
        client.configure_batch_size(args.batch_size)
        configured = True

    if args.channel is not None:
        client.configure_channel(args.channel)
        configured = True

    if configured:
        print("Configuration sent")
    else:
        print("No configuration options specified")
        sys.exit(1)


# =============================================================================
# Argument parsing
# =============================================================================


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Data Acquisition Client for LPC1768 ADC System",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s start --threshold-mv 1650 --channel 0           # Required: threshold + channel
    %(prog)s start --threshold-percent 50 --channel 1        # Or use percentage
    %(prog)s start --threshold-mv 1000 --channel 0 --save    # Save to auto-named SQLite
    %(prog)s start --threshold-mv 1000 --channel 0 --batch-size 200  # Custom batch size
    %(prog)s start --threshold-mv 1000 --channel 0 --log-level 0     # Debug logging
    %(prog)s status                                          # Get device status
    %(prog)s ping -c 5                                       # Ping 5 times
    %(prog)s configure --log-level 2                         # Set log to WARNING
    %(prog)s configure --reset-sequence                      # Reset packet counter

Defaults:
    - batch-size: 100 samples per packet
    - log-level: 2 (WARNING)
    - no-save: True (data not saved)
        """,
    )

    # Global options
    parser.add_argument(
        "-H",
        "--host",
        default="192.168.0.10",
        help="Target IP address (default: 192.168.0.10)",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=5000,
        help="Target UDP port (default: 5000)",
    )
    parser.add_argument(
        "-l",
        "--local-port",
        type=int,
        default=5001,
        help="Local UDP port (default: 5001)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose output",
    )
    parser.add_argument(
        "-d",
        "--debug",
        action="store_true",
        help="Enable debug output",
    )

    # Storage options
    storage_group = parser.add_argument_group("Storage options")
    storage_group.add_argument(
        "--no-save",
        action="store_true",
        default=True,
        help="Do not save data to file (default: True)",
    )
    storage_group.add_argument(
        "--save",
        action="store_false",
        dest="no_save",
        help="Enable saving data to file",
    )
    storage_group.add_argument(
        "--sqlite",
        metavar="FILE",
        help="Save to SQLite database file",
    )
    storage_group.add_argument(
        "--binary",
        metavar="FILE",
        help="Save to binary file",
    )
    storage_group.add_argument(
        "-o",
        "--output-dir",
        metavar="DIR",
        help="Output directory for auto-named files",
    )
    storage_group.add_argument(
        "-f",
        "--format",
        choices=["sqlite", "binary"],
        default="sqlite",
        help="Output format when using --output-dir (default: sqlite)",
    )

    # Subcommands
    subparsers = parser.add_subparsers(dest="command", help="Command to execute")

    # start - threshold and channel are required
    start_parser = subparsers.add_parser(
        "start",
        help="Start acquisition (requires --threshold-mv/--threshold-percent and --channel)",
    )
    _add_config_args(
        start_parser, required=False
    )  # We validate manually for better error msg

    # stop
    subparsers.add_parser("stop", help="Stop acquisition")

    # status
    subparsers.add_parser("status", help="Get device status")

    # ping
    ping_parser = subparsers.add_parser("ping", help="Ping the device")
    ping_parser.add_argument(
        "-c",
        "--count",
        type=int,
        default=4,
        help="Number of pings (default: 4)",
    )

    # configure - all options are optional
    config_parser = subparsers.add_parser("configure", help="Configure device")
    _add_config_args(config_parser, required=False)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    return args


def _add_config_args(parser: argparse.ArgumentParser, required: bool = False) -> None:
    """Add configuration arguments to a subparser."""
    # Required for start, optional for configure
    thresh_group = parser.add_mutually_exclusive_group(required=required)
    thresh_group.add_argument(
        "--threshold-percent",
        type=int,
        metavar="PCT",
        help="Set threshold as percentage (0-100)",
    )
    thresh_group.add_argument(
        "--threshold-mv",
        type=int,
        metavar="MV",
        help="Set threshold in millivolts (0-3300) [preferred]",
    )

    parser.add_argument(
        "--channel",
        type=int,
        metavar="CH",
        required=required,
        help="ADC channel (0-7)",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        metavar="N",
        default=None,
        help="Samples per packet (1-500, default: 100)",
    )
    parser.add_argument(
        "--log-level",
        type=int,
        choices=[0, 1, 2, 3, 4, 5],
        metavar="LVL",
        default=None,
        help="Device log level: 0=DEBUG, 1=INFO, 2=WARNING (default), 3=ERROR, 4=CRITICAL, 5=NONE",
    )
    parser.add_argument(
        "--reset-sequence",
        action="store_true",
        default=False,
        help="Reset sequence counter before starting",
    )


# =============================================================================
# Main entry point
# =============================================================================


def main() -> int:
    """Main entry point."""
    args = parse_args()
    setup_logging(args.verbose, args.debug)

    # Create storage backend
    storage = create_storage(args)

    # Create client
    client = DataAcquisitionClient(
        host=args.host,
        port=args.port,
        local_port=args.local_port,
        verbose=args.verbose,
    )

    # Handle SIGINT gracefully
    def signal_handler(_sig: int, _frame: object) -> None:
        client.stop()

    signal.signal(signal.SIGINT, signal_handler)

    try:
        # Dispatch command
        handlers = {
            "start": cmd_start,
            "stop": cmd_stop,
            "status": cmd_status,
            "ping": cmd_ping,
            "configure": cmd_configure,
        }

        handler = handlers.get(args.command)
        if handler:
            handler(client, args)
        else:
            logger.error("Unknown command: %s", args.command)
            return 1

    finally:
        if args.format == "sqlite" and isinstance(storage, SqliteStorage):
            storage.close()
        client.close()

    return 0
