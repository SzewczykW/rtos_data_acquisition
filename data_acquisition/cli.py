"""Command-line interface for data acquisition client.

NOTE: Data samples are printed to STDOUT (one CSV line per packet) so the output
can be piped to other tools. Logs go to STDERR via the standard ``logging``
module.
"""

from __future__ import annotations

import argparse
import logging
import sys
import time

from data_acquisition.client import DataAcquisitionClient

logger = logging.getLogger()


def cmd_start(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'start' command - configure, start acquisition, receive data.

    Args:
        client (DataAcquisitionClient): Client instance
        args (argparse.Namespace): Parsed command line arguments

    Returns: None
    """
    if _args.reset_sequence:
        client.reset_sequence()
        time.sleep(0.1)

    if _args.log_level is not None:
        client.set_log_level(_args.log_level)
        time.sleep(0.1)

    if _args.batch_size is not None:
        client.configure_batch_size(_args.batch_size)
        time.sleep(0.1)

    if _args.threshold_mv is not None:
        client.configure_threshold_mv(_args.threshold_mv)
        time.sleep(0.1)
    elif _args.threshold_percent is not None:
        client.configure_threshold_percent(_args.threshold_percent)
        time.sleep(0.1)

    if _args.channel is not None:
        client.configure_channel(_args.channel)
        time.sleep(0.1)

    client.start_acquisition()
    time.sleep(0.1)

    try:
        client.receive_loop(duration_s=_args.duration, max_samples=_args.samples)
    finally:
        client.stop_acquisition()


def cmd_stop(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'stop' command.

    Args:
        client (DataAcquisitionClient): Client instance
        args (argparse.Namespace): Parsed command line arguments

    Returns: None
    """
    client.stop_acquisition()
    logger.info("Stop command sent")


def cmd_status(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'status' command.

    Args:
        client (DataAcquisitionClient): Client instance
        args (argparse.Namespace): Parsed command line arguments

    Returns: None
    """
    status = client.get_status()

    if status:
        print(f"Acquiring:    {status.acquiring}")
        print(f"Channel:      {status.channel}")
        print(f"Threshold:    {status.threshold_mv} mV")
        print(f"Uptime:       {status.uptime} s")
        print(f"Samples sent: {status.samples_sent}")
    else:
        logger.error("Failed to get status")
        sys.exit(1)


def cmd_ping(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'ping' command.

    Args:
        client (DataAcquisitionClient): Client instance
        args (argparse.Namespace): Parsed command line arguments

    Returns: None
    """
    count = _args.count

    for i in range(count):
        rtt = client.ping()

        if rtt is not None:
            print(f"Pong from {client.host}: time={rtt:.2f} ms")
        else:
            print("Request timed out")

        if i < count - 1:
            time.sleep(1)


def cmd_configure(client: DataAcquisitionClient, _args: argparse.Namespace) -> None:
    """Handle 'configure' command.

    Args:
        client (DataAcquisitionClient): Client instance
        args (argparse.Namespace): Parsed command line arguments

    Returns: None
    """
    configured = False

    if _args.reset_sequence:
        client.reset_sequence()
        configured = True

    if _args.log_level is not None:
        configured = False
        client.set_log_level(_args.log_level)
        configured = True

    if _args.threshold_percent is not None:
        configured = False
        client.configure_threshold_percent(_args.threshold_percent)
        configured = True

    if _args.threshold_mv is not None:
        configured = False
        client.configure_threshold_mv(_args.threshold_mv)
        configured = True

    if _args.batch_size is not None:
        configured = False
        client.configure_batch_size(_args.batch_size)
        configured = True

    if _args.channel is not None:
        configured = False
        client.configure_channel(_args.channel)
        configured = True

    if configured:
        logger.info("Configuration sent")
    else:
        logger.error("No configuration options specified")
        sys.exit(1)


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
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
    %(prog)s start --duration 10 --log-level 0               # Device debug logging
    %(prog)s status                                          # Get device status
    %(prog)s ping -c 5                                       # Ping 5 times
    %(prog)s configure --log-level 2                         # Set device log to WARNING
    %(prog)s configure --reset-sequence                      # Reset packet counter

Defaults:
    - batch-size: 100 samples per packet
    - device log-level: 1 (INFO)
    - python logs: INFO
    - output: data to STDOUT (CSV lines)
        """,
    )

    parser.add_argument(
        "-H",
        "--host",
        default="10.10.10.25",
        help="Target IP address",
    )
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=5000,
        help="Target UDP port",
    )
    parser.add_argument(
        "--log",
        type=str.upper,
        choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
        default="INFO",
        help="Python logging level",
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
        help="Number of pings",
    )

    config_parser = subparsers.add_parser("configure", help="Configure device")
    _add_config_args(config_parser, required=False)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    return args


def _add_config_args(parser: argparse.ArgumentParser, required: bool = False) -> None:
    """Add configuration arguments to a subparser."""
    thresh_group = parser.add_mutually_exclusive_group(required=required)
    thresh_group.add_argument(
        "--threshold-percent",
        type=int,
        default=50,
        metavar="PCT",
        help="Set threshold as percentage (0-100)",
    )
    thresh_group.add_argument(
        "--threshold-mv",
        type=int,
        default=1650,
        metavar="MV",
        help="Set threshold in millivolts (0-3300)",
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
        default=100,
        help="Samples per packet (1-500)",
    )
    parser.add_argument(
        "--log-level",
        type=int,
        choices=[0, 1, 2, 3, 4, 5],
        metavar="LVL",
        default=1,
        help="Device log level: 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR, 4=CRITICAL, 5=NONE",
    )
    parser.add_argument(
        "--reset-sequence",
        action="store_true",
        default=False,
        help="Reset sequence counter before starting",
    )


def main() -> int:
    """Main entry point."""
    args = parse_args()

    logging.basicConfig(
        level=args.log,
        format="%(asctime)s.%(msecs)03d [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    client = DataAcquisitionClient(
        host=args.host,
        port=args.port,
    )

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
