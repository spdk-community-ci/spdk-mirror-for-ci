#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2018 Intel Corporation.
#  All rights reserved.

import os
import re
import json
import logging
from dataclasses import dataclass, field
from subprocess import check_output
from collections import OrderedDict
from json.decoder import JSONDecodeError


@dataclass
class LatencyData:
    avg_lat: float
    min_lat: float
    max_lat: float
    percentiles: list = field(default_factory=list)
    unit: str = "us"
    unit_p99: str = "us"

    def __add__(self, new_data):
        return LatencyData(
            avg_lat=self.avg_lat + new_data.avg_lat,
            min_lat=self.min_lat + new_data.min_lat,
            max_lat=self.max_lat + new_data.max_lat,
            percentiles=[p1 + p2 for p1, p2 in zip(self.percentiles, new_data.percentiles)],
            unit=self.unit
        )

    def average(self, count):
        self.avg_lat /= count
        self.min_lat /= count
        self.max_lat /= count
        self.percentiles = [p / count for p in self.percentiles]

    def convert_ns_to_us(self):
        if self.unit == "ns":
            self.avg_lat /= 1000
            self.min_lat /= 1000
            self.max_lat /= 1000
        if self.unit_p99 == "ns":
            self.percentiles = [p / 1000 for p in self.percentiles]

    def get_metrics(self):
        return [self.avg_lat, self.min_lat, self.max_lat] + self.percentiles


@dataclass
class PerformanceMetrics:
    read_iops: float
    read_bw: float
    read_latency: LatencyData
    write_iops: float
    write_bw: float
    write_latency: LatencyData

    def to_list(self):
        metrics = [self.read_iops, self.read_bw, *self.read_latency.get_metrics(),
                   self.write_iops, self.write_bw, *self.write_latency.get_metrics()]
        return [round(m, 3) for m in metrics]


def read_json_stats(file):
    with open(file, "r") as json_data:
        data = json.load(json_data)

        # Check if latency is in nano or microseconds to choose correct dict key
        def get_lat_unit(key_prefix, dict_section):
            # key prefix - lat, clat or slat.
            # dict section - portion of json containing latency bucket in question
            # Return dict key to access the bucket and unit as string
            for k, _ in dict_section.items():
                if k.startswith(key_prefix):
                    return k, k.split("_")[1]

        def get_clat_percentiles(clat_dict_leaf):
            if "percentile" in clat_dict_leaf:
                p99_lat = float(clat_dict_leaf["percentile"]["99.000000"])
                p99_9_lat = float(clat_dict_leaf["percentile"]["99.900000"])
                p99_99_lat = float(clat_dict_leaf["percentile"]["99.990000"])
                p99_999_lat = float(clat_dict_leaf["percentile"]["99.999000"])

                return [p99_lat, p99_9_lat, p99_99_lat, p99_999_lat]
            else:
                # Latest fio versions do not provide "percentile" results if no
                # measurements were done, so just return zeroes
                return [0, 0, 0, 0]

        def extract_latency_data(job_data, rw_operation):
            lat_key, lat_unit = get_lat_unit("lat", job_data[rw_operation])
            lat_data = job_data[rw_operation][lat_key]
            avg_lat = float(lat_data["mean"])
            min_lat = float(lat_data["min"])
            max_lat = float(lat_data["max"])

            clat_key, clat_unit = get_lat_unit("clat", job_data[rw_operation])
            percentiles = get_clat_percentiles(job_data[rw_operation][clat_key])

            lat_data = LatencyData(avg_lat, min_lat, max_lat, percentiles, lat_unit, clat_unit)
            lat_data.convert_ns_to_us()

            return lat_data

        total_read_iops = 0
        total_read_bw = 0
        total_write_iops = 0
        total_write_bw = 0
        total_read_latency = LatencyData(0, 0, 0, [0, 0, 0, 0])
        total_write_latency = LatencyData(0, 0, 0, [0, 0, 0, 0])

        fio_job_count = len(data["jobs"])
        for fio_job in data["jobs"]:
            fio_job_name = fio_job.get("jobname", False)
            if not fio_job_name:
                continue

            total_read_iops += float(fio_job["read"]["iops"])
            total_read_bw += float(fio_job["read"]["bw"])
            total_read_latency += extract_latency_data(fio_job, "read")

            total_write_iops += float(fio_job["write"]["iops"])
            total_write_bw += float(fio_job["write"]["bw"])
            total_write_latency += extract_latency_data(fio_job, "write")

        total_read_latency.average(fio_job_count)
        total_write_latency.average(fio_job_count)
        performance_metrics = PerformanceMetrics(total_read_iops, total_read_bw, total_read_latency,
                                                 total_write_iops, total_write_bw, total_write_latency)

    return performance_metrics.to_list()


def read_target_stats(measurement_name, results_file_list, results_dir):
    # Read additional metrics measurements done on target side and
    # calculate the average from across all workload iterations.
    # Currently only works for SAR CPU utilization and power draw measurements.
    # Other (bwm-ng, pcm, dpdk memory) need to be refactored and provide more
    # structured result files instead of a output dump.
    total_util = 0
    for result_file in results_file_list:
        with open(os.path.join(results_dir, result_file), "r") as result_file_fh:
            total_util += float(result_file_fh.read())
    avg_util = total_util / len(results_file_list)

    return {measurement_name: "{0:.3f}".format(avg_util)}


def parse_results(results_dir, csv_file):
    files = os.listdir(results_dir)
    fio_files = filter(lambda x: ".fio" in x, files)
    json_files = [x for x in files if ".json" in x]
    sar_files = [x for x in files if "sar" in x and "util" in x]
    pm_files = [x for x in files if "pm" in x and "avg" in x]

    headers = ["read_iops", "read_bw", "read_avg_lat_us", "read_min_lat_us", "read_max_lat_us",
               "read_p99_lat_us", "read_p99.9_lat_us", "read_p99.99_lat_us", "read_p99.999_lat_us",
               "write_iops", "write_bw", "write_avg_lat_us", "write_min_lat_us", "write_max_lat_us",
               "write_p99_lat_us", "write_p99.9_lat_us", "write_p99.99_lat_us", "write_p99.999_lat_us"]

    header_line = ",".join(["Name", *headers])
    rows = set()

    for fio_config in fio_files:
        logging.info("Getting FIO stats for %s" % fio_config)
        job_name, _ = os.path.splitext(fio_config)
        aggr_headers = ["iops", "bw", "avg_lat_us", "min_lat_us", "max_lat_us",
                        "p99_lat_us", "p99.9_lat_us", "p99.99_lat_us", "p99.999_lat_us"]

        # Look in the filename for rwmixread value. Function arguments do
        # not have that information.
        # TODO: Improve this function by directly using workload params instead
        # of regexing through filenames.
        if "read" in job_name:
            rw_mixread = 1
        elif "write" in job_name:
            rw_mixread = 0
        else:
            rw_mixread = float(re.search(r"m_(\d+)", job_name).group(1)) / 100

        # If "_CPU" exists in name - ignore it
        # Initiators for the same job could have different num_cores parameter
        job_name = re.sub(r"_\d+CPU", "", job_name)
        job_result_files = [x for x in json_files if x.startswith(job_name)]
        sar_result_files = [x for x in sar_files if x.startswith(job_name)]

        # Collect all pm files for the current job
        job_pm_files = [x for x in pm_files if x.startswith(job_name)]

        # Filter out data from DCMI sensors and socket/dram sensors
        dcmi_sensors = [x for x in job_pm_files if "DCMI" in x]
        socket_dram_sensors = [x for x in job_pm_files if "DCMI" not in x and ("socket" in x or "dram" in x)]
        sdr_sensors = list(set(job_pm_files) - set(dcmi_sensors) - set(socket_dram_sensors))

        # Determine the final list of pm_result_files, if DCMI file is present, use it as a primary source
        # of power consumption data. If not, use SDR sensors data if available. If SDR sensors are not available,
        # use socket and dram sensors as a fallback.
        pm_result_files = dcmi_sensors or sdr_sensors
        if not pm_result_files and socket_dram_sensors:
            logging.warning("No DCMI or SDR data found for %s, using socket and dram data sensors as a fallback" % job_name)
            pm_result_files = socket_dram_sensors

        logging.info("Matching result files for current fio config %s:" % job_name)
        for j in job_result_files:
            logging.info("\t %s" % j)

        # There may have been more than 1 initiator used in test, need to check that
        # Result files are created so that string after last "_" separator is server name
        inits_names = set([os.path.splitext(x)[0].split("_")[-1] for x in job_result_files])
        inits_avg_results = []
        for i in inits_names:
            logging.info("\tGetting stats for initiator %s" % i)
            # There may have been more than 1 test run for this job, calculate average results for initiator
            i_results = [x for x in job_result_files if i in x]
            i_results_filename = re.sub(r"run_\d+_", "", i_results[0].replace("json", "csv"))

            separate_stats = []
            for r in i_results:
                try:
                    stats = read_json_stats(os.path.join(results_dir, r))
                    separate_stats.append(stats)
                    logging.info([float("{0:.3f}".format(x)) for x in stats])
                except JSONDecodeError:
                    logging.error("ERROR: Failed to parse %s results! Results might be incomplete!" % r)

            init_results = [sum(x) for x in zip(*separate_stats)]
            init_results = [x / len(separate_stats) for x in init_results]
            init_results = [round(x, 3) for x in init_results]
            inits_avg_results.append(init_results)

            logging.info("\tAverage results for initiator %s" % i)
            logging.info(init_results)
            with open(os.path.join(results_dir, i_results_filename), "w") as fh:
                fh.write(header_line + "\n")
                fh.write(",".join([job_name, *["{0:.3f}".format(x) for x in init_results]]) + "\n")

        # Sum results of all initiators running this FIO job.
        # Latency results are an average of latencies from across all initiators.
        inits_avg_results = [sum(x) for x in zip(*inits_avg_results)]
        inits_avg_results = OrderedDict(zip(headers, inits_avg_results))
        for key in inits_avg_results:
            if "lat" in key:
                inits_avg_results[key] /= len(inits_names)

        # Aggregate separate read/write values into common labels
        # Take rw_mixread into consideration for mixed read/write workloads.
        aggregate_results = OrderedDict()
        for h in aggr_headers:
            read_stat, write_stat = [float(value) for key, value in inits_avg_results.items() if h in key]
            if "lat" in h:
                _ = rw_mixread * read_stat + (1 - rw_mixread) * write_stat
            else:
                _ = read_stat + write_stat
            aggregate_results[h] = "{0:.3f}".format(_)

        if sar_result_files:
            aggr_headers.append("target_avg_cpu_util")
            aggregate_results.update(read_target_stats("target_avg_cpu_util", sar_result_files, results_dir))

        if pm_result_files:
            aggr_headers.append("target_avg_power")
            aggregate_results.update(read_target_stats("target_avg_power", pm_result_files, results_dir))

        rows.add(",".join([job_name, *aggregate_results.values()]))

    # Create empty results file with just the header line
    aggr_header_line = ",".join(["Name", *aggr_headers])
    with open(os.path.join(results_dir, csv_file), "w") as fh:
        fh.write(aggr_header_line + "\n")

    # Save results to file
    for row in rows:
        with open(os.path.join(results_dir, csv_file), "a") as fh:
            fh.write(row + "\n")
    logging.info("You can find the test results in the file %s" % os.path.join(results_dir, csv_file))
