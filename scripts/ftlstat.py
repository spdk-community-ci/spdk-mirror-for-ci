#!/usr/bin/env python3
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright 2023 Solidigm All Rights Reserved
#

import json
import logging
import os
import sys
import argparse
import time
import datetime
import signal
import pandas
from threading import Event
from rich.console import Console
from rich.table import Table
from rich.style import Style
from rich.color import Color

sys.path.append(os.path.dirname(__file__) + '/../python')

import spdk.rpc as rpc  # noqa


class FTLBdev:
    name: str = ""
    current: dict = {}
    previous: dict = {}
    delta: dict = {}
    active: bool = True

    def __init__(self, name: str):
        self.name = name

    def get_stats(self):
        self.current = rpc.bdev.bdev_ftl_get_stats(args.client, self.name)
        self.current['time'] = datetime.datetime.now().isoformat()
        self.current['interval'] = time.time_ns()

    def process(self):
        self.get_stats()

    def get_delta(self):
        self.delta = {}
        interval = 0.0

        if 'interval' in self.previous and 'interval' in self.current:
            interval = (self.current['interval'] -
                        self.previous['interval']) / 1e9

        self.__get_delta(self.delta, self.previous, self.current, interval)
        self.previous = self.current
        return self.delta

    def __delta_number_decimal_digits(self, value):
        # Convert the number to a string if it's not already
        string = str(value)
        integer, _, fractional = string.partition('.')
        return len(fractional)

    def __delta_number_format(self, value, digits):
        format = "{:.%df}" % digits
        number = format.format(value)
        return number

    def __delta_number(self, key, delta, previous, current, interval):
        try:
            current_value = float(current[key])
            previous_value = float(previous[key])
            value = (current_value - previous_value) / interval
            decimal_digits = max(self.__delta_number_decimal_digits(
                current[key]), self.__delta_number_decimal_digits(previous[key]))
            delta[key] = self.__delta_number_format(value, decimal_digits)
            return True
        except ValueError:
            return False

    def __get_delta(self, delta, previous, current, interval):
        for key in previous:
            if key in current:
                if isinstance(previous[key], dict) and isinstance(current[key], dict):
                    delta[key] = {}
                    self.__get_delta(
                        delta[key], previous[key], current[key], interval)
                elif isinstance(previous[key], (int, float)) and isinstance(current[key], (int, float)):
                    delta[key] = (current[key] - previous[key]) / interval
                elif self.__delta_number(key, delta, previous, current, interval):
                    continue
                elif isinstance(previous[key], list) and isinstance(current[key], list):
                    raise Exception(
                        "Not supported JSON element to compute delta")
                else:
                    delta[key] = current[key]


class FTLStat:
    def __init__(self, args):
        signal.signal(signal.SIGINT, lambda signal,
                      frame: self._terminate_signal_handler())
        signal.signal(signal.SIGTERM, lambda signal,
                      frame: self._terminate_signal_handler())

        self.args = args
        self.ftl_bdevs = []
        self.exit = Event()
        self.first = True

        self.csv_hdr = None
        self.flat_stats = {}

    def _terminate_signal_handler(self):
        self.exit.set()

    def _get_bdevs(self):
        ftl: FTLBdev
        for ftl in self.ftl_bdevs:
            ftl.active = False

        bdevs = rpc.bdev.bdev_get_bdevs(self.args.client)
        for bdev in bdevs:
            if 'ftl' in bdev.get('driver_specific', {}):

                # Check if device already on the list
                found = False
                for ftl in self.ftl_bdevs:
                    if ftl.name == bdev['name']:
                        ftl.active = True
                        found = True
                        break

                if not found:
                    self.ftl_bdevs.append(FTLBdev(bdev['name']))

        self.ftl_bdevs = [ftl for ftl in self.ftl_bdevs if ftl.active]

    def __flat_stats(self, stats: dict, flat_path: str, flat_stats: dict):
        for key in stats:
            flat_key = flat_path
            if flat_key != "":
                flat_key = flat_path + "." + key
            else:
                flat_key = key

            if isinstance(stats[key], dict):
                self.__flat_stats(stats[key], flat_key, flat_stats)
            elif isinstance(stats[key], list):
                raise Exception("Not supported JSON element to compute delta")
            else:
                flat_stats[flat_key] = stats[key]

    def _flat_stats(self, stats, flat_stats):
        flat_path = ""
        self.__flat_stats(stats, flat_path, flat_stats)

    def _flat_stats_keys(self, flat_stats: dict, flat_stats_keys: list):
        for key in flat_stats:
            if key not in flat_stats_keys:
                flat_stats_keys.append(key)

    def _flat_stats_io(self, flat_stats: dict, flat_stats_io: dict):
        for key, value in flat_stats.items():
            if key.endswith(".ios"):
                flat_stats_io[key] = value / 1000.0  # kIOPS
            elif key.endswith(".blocks"):
                key = ".bw".join(key.rsplit(".blocks", 1))
                value = value * 4096 / 2**20  # MiB/s
                flat_stats_io[key] = value

    def _output_start(self):
        if args.format == 'json':
            print("[")

    def _output_stop(self):
        if args.format == 'json':
            print("]")

    def _output(self, stats):
        if not stats:
            return
        if args.format == 'json':
            if self.first is not True:
                print(",")
            print(json.dumps(stats, indent=2))
            self.first = False
        elif args.format == 'table':
            flat_stats = {}
            self._flat_stats(stats, flat_stats)
            flat_stats_io = {}
            self._flat_stats_io(flat_stats, flat_stats_io)
            self.flat_stats[stats['name']] = flat_stats_io
        elif args.format == 'csv':
            if self.first is True:
                self.csv_hdr = pandas.json_normalize(stats)
                print(self.csv_hdr.to_csv(index=False, header=True), end="")
                self.first = False
            else:
                df = pandas.json_normalize(stats).reindex(
                    columns=self.csv_hdr.columns)
                print(df.to_csv(index=False, header=False), end="")

    def _output_flush(self):
        if args.format == 'table' and self.flat_stats:
            table = Table(title=str("FTL Stats, " +
                          datetime.datetime.now().isoformat()))

            color = Color.from_rgb(79, 0, 181)
            style = Style(color=color, bold=True)
            table.add_column("Value", style=style)

            style = Style(color="bright_blue", italic=True)
            table.add_column("Unit", style=style)

            values = []
            style = Style(color="green", bold=True)
            for ftl in self.flat_stats.keys():
                table.add_column(ftl, style=style,
                                 justify="right", min_width=7)
                self._flat_stats_keys(self.flat_stats[ftl], values)
            values = sorted(values)

            section = values[0].split(".")[0]
            for metric in values:
                row = [metric]

                if metric.endswith(".ios"):
                    row.append("kIOPS")
                elif metric.endswith(".bw"):
                    row.append("MiB/s")
                else:
                    row.append("")

                for key in self.flat_stats.keys():
                    stat = self.flat_stats[key]
                    value = float(stat[metric])
                    row.append(f"{value:.1f}")

                prefix = metric.split(".")[0]
                if prefix != section:
                    table.add_section()
                table.add_row(*row)
                section = prefix

            console = Console()
            print("")
            console.print(table)
            self.flat_stats = {}

    def run(self):
        self._output_start()

        while not self.exit.is_set():
            self._get_bdevs()
            time_start = datetime.datetime.now()

            # Iterate over FTL bdevs and output data
            ftl: FTLBdev
            for ftl in self.ftl_bdevs:
                ftl.process()
                delta = ftl.get_delta()
                self._output(delta)

            self._output_flush()
            time_sleep = args.interval - \
                (datetime.datetime.now() - time_start).total_seconds()
            if time_sleep > 0:
                self.exit.wait(time_sleep)

        self._output_stop()


def check_positive(value):
    v = int(value)
    if v <= 0:
        raise argparse.ArgumentTypeError("%s should be positive int value" % v)
    return v


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='SPDK ftlstat command line interface')

    parser.add_argument('-s', "--server", dest='server_addr',
                        help='RPC domain socket path or IP address',
                        default='/var/tmp/spdk.sock')

    parser.add_argument('-p', "--port", dest='port',
                        help='RPC port number (if server_addr is IP address)',
                        default=4420, type=int)

    parser.add_argument('--format', choices=['json', 'csv', 'table'],
                        help='Specify the output format, default is json',
                        required=False, default='json')

    parser.add_argument('-o', '--timeout', dest='timeout',
                        help='Timeout as a floating point number expressed in seconds \
                        waiting for response. Default: 60.0',
                        default=60.0, type=float)

    parser.add_argument('-v', dest='verbose', action='store_const', const="INFO",
                        help='Set verbose mode to INFO', default="ERROR")

    parser.add_argument('-i', '--interval', dest='interval',
                        type=check_positive, help='Time interval (in seconds) on which \
                        to poll FTL stats.',
                        required=False, default=10)

    args = parser.parse_args()

    args.client = rpc.client.JSONRPCClient(args.server_addr, args.port, args.timeout, log_level=getattr(
        logging, args.verbose.upper()))

    ftlstat = FTLStat(args)
    ftlstat.run()
