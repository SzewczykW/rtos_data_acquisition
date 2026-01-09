"""Command-line interface for data acquisition client."""

from __future__ import annotations

import argparse
import logging
import signal
import sys
import time
from datetime import datetime
from pathlib import Path
from types import FrameType
from typing import Any

from data_acquisition.client import DataAcquisitionClient
from data_acquisition.storage.binary import BinaryStorage

logger = logging.getLogger()


def setup_logging(verbose: bool, debug: bool) -> None:
    """
    Configure logging based on verbosity flags.

    Args:
        verbose: Enable INFO level logging
        debug: Enable DEBUG level logging
    """
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


def create_storage(args: argparse.Namespace) -> BinaryStorage | None:
    """
    Create appropriate storage backend based on CLI arguments.

    Args:
        args: Parsed command line arguments
    Returns:
        Storage backend instance or None if storage is disabled
    """
    if args.no_save:
        logger.debug("Storage disabled (--no-save)")
        return None

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    if args.output_dir:
        output_dir = Path(args.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        return BinaryStorage(output_dir / f"acquisition_{timestamp}.bin")

    logger.error("Output directory not specified for storage")
    return None


def cmd_start(client: DataAcquisitionClient, args: argparse.Namespace) -> None:
    """
    Handle 'start' command - configure, start acquisition, receive data.

    Args:
        client: Data acquisition client instance
        args: Parsed command line arguments
    """
    if args.reset_sequence:
        client.reset_sequence()
        time.sleep(0.1)

    if args.log_level is not None:
        client.set_log_level(args.log_level)
        time.sleep(0.1)

    if args.batch_size is not None:
        client.configure_batch_size(args.batch_size)
        time.sleep(0.1)

    if args.threshold_mv is not None:
        client.configure_threshold_mv(args.threshold_mv)
        time.sleep(0.1)
    elif args.threshold_percent is not None:
        client.configure_threshold_percent(args.threshold_percent)
        time.sleep(0.1)

    if args.channel is not None:
        client.configure_channel(args.channel)
        time.sleep(0.1)

    client.start_acquisition()
    time.sleep(0.1)

    # Deterministic stop condition (no Ctrl+C needed): duration OR samples is required.
    try:
        client.receive_loop(duration_s=args.duration, max_samples=args.samples)
    finally:
        client.stop_acquisition()


def cmd_stop(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """
    Handle 'stop' command.

    Args:
        client: Data acquisition client instance
        _args: Parsed command line arguments
    """
    client.stop_acquisition()
    print("Stop command sent")


def cmd_status(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """
    Handle 'status' command.

    Args:
        client: Data acquisition client instance
        _args: Parsed command line arguments
    """
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
    """
    Handle 'ping' command.

    Args:
        client: Data acquisition client instance
        args: Parsed command line arguments
    """
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
    """
    Handle 'configure' command.

    Args:
        client: Data acquisition client instance
        args: Parsed command line arguments
    """
    configured = False

    if args.reset_sequence:
        client.reset_sequence()
        configured = True

    if args.log_level is not None:
        client.set_log_level(args.log_level)
        configured = True

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


def parse_args() -> argparse.Namespace:
    """
    Parse command line arguments.
    Returns:
        Parsed arguments namespace
    """
    parser = argparse.ArgumentParser(
        description="Data Acquisition Client for LPC1768 ADC System",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    %(prog)s start --duration 5                              # Acquire for 5 seconds (no reconfig)
    %(prog)s start --samples 20000                           # Acquire until at least 20000 samples
    %(prog)s start --duration 10 --threshold-mv 1650 --channel 0
    %(prog)s start --samples 50000 --threshold-percent 50 --channel 1
    %(prog)s start --duration 10 --batch-size 200            # Custom batch size
    %(prog)s start --duration 10 --log-level 0               # Debug logging
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

    storage_group = parser.add_argument_group("Storage options")
    storage_group.add_argument(
        "--no-save",
        action="store_true",
        default=True,
        help="Do not save data to file (default: True)",
    )
    storage_group.add_argument(
        "-o",
        "--output-dir",
        metavar="DIR",
        help="Output directory for auto-named files",
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to execute")

    start_parser = subparsers.add_parser(
        "start",
        help="Start acquisition (requires --duration or --samples; configuration args are optional)",
    )

    stop_group = start_parser.add_mutually_exclusive_group(required=True)
    stop_group.add_argument(
        "--duration",
        type=float,
        metavar="SEC",
        help="How long to acquire data, in seconds",
    )
    stop_group.add_argument(
        "--samples",
        type=int,
        metavar="N",
        help="How many samples to acquire (stop after reaching at least this value)",
    )

    _add_config_args(start_parser, required=False)

    subparsers.add_parser("stop", help="Stop acquisition")

    subparsers.add_parser("status", help="Get device status")

    ping_parser = subparsers.add_parser("ping", help="Ping the device")
    ping_parser.add_argument(
        "-c",
        "--count",
        type=int,
        default=4,
        help="Number of pings (default: 4)",
    )

    config_parser = subparsers.add_parser("configure", help="Configure device")
    _add_config_args(config_parser, required=False)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    return args


def _add_config_args(parser: argparse.ArgumentParser, required: bool = False) -> None:
    """
    Add configuration arguments to a subparser.

    Args:
        parser: Subparser to add arguments to
        required: Whether threshold and channel arguments are required
    """
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


def main() -> int:
    """
    Main entry point.
    """
    args = parse_args()
    setup_logging(args.verbose, args.debug)

    storage = create_storage(args)

    client = DataAcquisitionClient(
        host=args.host,
        port=args.port,
        storage=storage,
        verbose=args.verbose,
    )

    def __signal_handler(_sig: int, _frame: FrameType | None) -> Any:
        # Best-effort graceful shutdown on Ctrl+C.
        # Don't close the socket here (it will be closed in the main finally block),
        # otherwise we risk double-close and duplicate stats output.
        try:
            client.stop()
            client.stop_acquisition()
        except Exception:
            pass

    signal.signal(signal.SIGINT, __signal_handler)

    try:
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
        client.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
