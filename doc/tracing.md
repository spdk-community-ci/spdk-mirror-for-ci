# Tracing Framework {#tracepoints}

## Introduction {#tracepoints_intro}

SPDK has a tracing framework for capturing low-level event information at runtime.
Tracepoints provide a high-performance tracing mechanism that is accessible at runtime.
They are implemented as a circular buffer in shared memory that is accessible from other
processes. SPDK libraries and modules are instrumented with tracepoints to enable analysis of
both performance and application crashes.

## Enabling Tracepoints {#enable_tracepoints}

Tracepoints are placed in groups. They are enabled and disabled as a group or individually
inside a group.

### Enabling Tracepoints in Groups

To enable the instrumentation of all the tracepoints groups in an SPDK target
application, start the target with `-e` parameter set to `all`:

~~~bash
build/bin/nvmf_tgt -e all
~~~

To enable the instrumentation of just the `NVMe-oF RDMA` tracepoints in an SPDK target
application, start the target with the `-e` parameter set to `nvmf_rdma`:

~~~bash
build/bin/nvmf_tgt -e nvmf_rdma
~~~

### Enabling Individual Tracepoints

To enable individual tracepoints inside a group:

~~~bash
build/bin/nvmf_tgt -e nvmf_rdma:B
~~~

where `:` is a separator and `B` is the tracepoint mask. This will enable only the first, second and fourth (binary: 1011) tracepoint inside `NVMe-oF RDMA` group.

### Combining Tracepoint Masks

It is also possible to combine enabling whole groups of tpoints and individual ones:

~~~bash
build/bin/nvmf_tgt -e nvmf_rdma:2,thread
~~~

This will enable the second tracepoint inside `NVMe-oF RDMA` group (0x10) and all of the tracepoints defined by the `thread` group (0x400).

### Tracepoint Group Names

The best way to get the current list of group names is from an application's
help message. The list of available tracepoint group's for that application
will be shown next to the `-e` option.

### Starting the SPDK Target

When the target starts, a message is logged with the information you need to view
the tracepoints in a human-readable format using the spdk_trace application. The target
will also log information about the shared memory file.

~~~bash
app.c: 527:spdk_app_setup_trace: *NOTICE*: Tracepoint Group Mask 0xFFFF specified.
app.c: 531:spdk_app_setup_trace: *NOTICE*: Use 'spdk_trace -s nvmf -p 24147' to capture a snapshot of events at runtime.
app.c: 533:spdk_app_setup_trace: *NOTICE*: Or copy /dev/shm/nvmf_trace.pid24147 for offline analysis/debug.
~~~

Note that when tracepoints are enabled, the shared memory files are not deleted when the application
exits.  This ensures the file can be used for analysis after the application exits.  On Linux, the
shared memory files are in /dev/shm, and can be deleted manually to free shm space if needed.  A system
reboot will also free all of the /dev/shm files.

## Capturing a snapshot of events {#capture_tracepoints}

Send I/Os to the SPDK target application to generate events. The following is
an example usage of spdk_nvme_perf to send I/Os to the NVMe-oF target over an RDMA network
interface for 10 minutes.

~~~bash
spdk_nvme_perf -q 128 -o 4096 -w randread -t 600 -r 'trtype:RDMA adrfam:IPv4 traddr:192.168.100.2 trsvcid:4420'
~~~

The spdk_trace program can be found in the app/trace directory.  To analyze the tracepoints on the same
system running the NVMe-oF target, simply execute the command line shown in the log:

~~~bash
build/bin/spdk_trace -s nvmf -p 24147
~~~

Note that you can also call `build/bin/spdk_trace` without any parameters on Linux, and it will
use the most recent SPDK trace file generated on that system.

To analyze the tracepoints on a different system, first prepare the tracepoint file for transfer.  The
tracepoint file can be large, but usually compresses very well.  This step can also be used to prepare
a tracepoint file to attach to a GitHub issue for debugging NVMe-oF application crashes.

~~~bash
bzip2 -c /dev/shm/nvmf_trace.pid24147 > /tmp/trace.bz2
~~~

After transferring the /tmp/trace.bz2 tracepoint file to a different system:

~~~bash
bunzip2 /tmp/trace.bz2
build/bin/spdk_trace -f /tmp/trace
~~~

The following is sample trace capture showing the cumulative time that each
I/O spends at each RDMA state. All the trace captures with the same id are for
the same I/O.

~~~bash
28:   6026.658 ( 12656064)     RDMA_REQ_NEED_BUFFER                                      id:    r3622            time:  0.019
28:   6026.694 ( 12656140)     RDMA_REQ_RDY_TO_EXECUTE                                   id:    r3622            time:  0.055
28:   6026.820 ( 12656406)     RDMA_REQ_EXECUTING                                        id:    r3622            time:  0.182
28:   6026.992 ( 12656766)     RDMA_REQ_EXECUTED                                         id:    r3477            time:  228.510
28:   6027.010 ( 12656804)     RDMA_REQ_TX_PENDING_C_TO_H                                id:    r3477            time:  228.528
28:   6027.022 ( 12656828)     RDMA_REQ_RDY_TO_COMPLETE                                  id:    r3477            time:  228.539
28:   6027.115 ( 12657024)     RDMA_REQ_COMPLETING                                       id:    r3477            time:  228.633
28:   6027.471 ( 12657770)     RDMA_REQ_COMPLETED                                        id:    r3518            time:  171.577
28:   6028.027 ( 12658940)     RDMA_REQ_NEW                                              id:    r3623
28:   6028.057 ( 12659002)     RDMA_REQ_NEED_BUFFER                                      id:    r3623            time:  0.030
28:   6028.095 ( 12659082)     RDMA_REQ_RDY_TO_EXECUTE                                   id:    r3623            time:  0.068
28:   6028.216 ( 12659336)     RDMA_REQ_EXECUTING                                        id:    r3623            time:  0.189
28:   6028.408 ( 12659740)     RDMA_REQ_EXECUTED                                         id:    r3505            time:  190.509
28:   6028.441 ( 12659808)     RDMA_REQ_TX_PENDING_C_TO_H                                id:    r3505            time:  190.542
28:   6028.452 ( 12659832)     RDMA_REQ_RDY_TO_COMPLETE                                  id:    r3505            time:  190.553
28:   6028.536 ( 12660008)     RDMA_REQ_COMPLETING                                       id:    r3505            time:  190.637
28:   6028.854 ( 12660676)     RDMA_REQ_COMPLETED                                        id:    r3465            time:  247.000
28:   6029.433 ( 12661892)     RDMA_REQ_NEW                                              id:    r3624
28:   6029.452 ( 12661932)     RDMA_REQ_NEED_BUFFER                                      id:    r3624            time:  0.019
28:   6029.482 ( 12661996)     RDMA_REQ_RDY_TO_EXECUTE                                   id:    r3624            time:  0.050
28:   6029.591 ( 12662224)     RDMA_REQ_EXECUTING                                        id:    r3624            time:  0.158
28:   6029.782 ( 12662624)     RDMA_REQ_EXECUTED                                         id:    r3564            time:  96.937
28:   6029.798 ( 12662658)     RDMA_REQ_TX_PENDING_C_TO_H                                id:    r3564            time:  96.953
28:   6029.812 ( 12662688)     RDMA_REQ_RDY_TO_COMPLETE                                  id:    r3564            time:  96.967
28:   6029.899 ( 12662870)     RDMA_REQ_COMPLETING                                       id:    r3564            time:  97.054
28:   6030.262 ( 12663634)     RDMA_REQ_COMPLETED                                        id:    r3477            time:  231.780
28:   6030.786 ( 12664734)     RDMA_REQ_NEW                                              id:    r3625
28:   6030.804 ( 12664772)     RDMA_REQ_NEED_BUFFER                                      id:    r3625            time:  0.018
28:   6030.841 ( 12664848)     RDMA_REQ_RDY_TO_EXECUTE                                   id:    r3625            time:  0.054
28:   6030.963 ( 12665104)     RDMA_REQ_EXECUTING                                        id:    r3625            time:  0.176
28:   6031.139 ( 12665474)     RDMA_REQ_EXECUTED                                         id:    r3552            time:  114.906
28:   6031.196 ( 12665594)     RDMA_REQ_TX_PENDING_C_TO_H                                id:    r3552            time:  114.963
28:   6031.210 ( 12665624)     RDMA_REQ_RDY_TO_COMPLETE                                  id:    r3552            time:  114.977
28:   6031.293 ( 12665798)     RDMA_REQ_COMPLETING                                       id:    r3552            time:  115.060
28:   6031.633 ( 12666512)     RDMA_REQ_COMPLETED                                        id:    r3505            time:  193.734
28:   6032.230 ( 12667766)     RDMA_REQ_NEW                                              id:    r3626
28:   6032.248 ( 12667804)     RDMA_REQ_NEED_BUFFER                                      id:    r3626            time:  0.018
28:   6032.288 ( 12667888)     RDMA_REQ_RDY_TO_EXECUTE                                   id:    r3626            time:  0.058
28:   6032.396 ( 12668114)     RDMA_REQ_EXECUTING                                        id:    r3626            time:  0.166
28:   6032.593 ( 12668528)     RDMA_REQ_EXECUTED                                         id:    r3570            time:  90.443
28:   6032.611 ( 12668564)     RDMA_REQ_TX_PENDING_C_TO_H                                id:    r3570            time:  90.460
28:   6032.623 ( 12668590)     RDMA_REQ_RDY_TO_COMPLETE                                  id:    r3570            time:  90.473
28:   6032.707 ( 12668766)     RDMA_REQ_COMPLETING                                       id:    r3570            time:  90.557
28:   6033.056 ( 12669500)     RDMA_REQ_COMPLETED                                        id:    r3564            time:  100.211
~~~

## Capturing sufficient trace events {#capture_trace_events}

Since the tracepoint file generated directly by SPDK application is a circular buffer in shared memory,
the trace events captured by it may be insufficient for further analysis.
The spdk_trace_record program can be found in the app/trace_record directory.
spdk_trace_record is used to poll the spdk tracepoint shared memory, record new entries from it,
and store all entries into specified output file at its shutdown on SIGINT or SIGTERM.
After SPDK nvmf target is launched, simply execute the command line shown in the log:

~~~bash
build/bin/spdk_trace_record -q -s nvmf -p 24147 -f /tmp/spdk_nvmf_record.trace
~~~

Also send I/Os to the SPDK target application to generate events by previous perf example for 10 minutes.

~~~bash
spdk_nvme_perf -q 128 -o 4096 -w randread -t 600 -r 'trtype:RDMA adrfam:IPv4 traddr:192.168.100.2 trsvcid:4420'
~~~

After the completion of perf example, shut down spdk_trace_record by signal SIGINT (Ctrl + C).
To analyze the tracepoints output file from spdk_trace_record, simply run spdk_trace program by:

~~~bash
build/bin/spdk_trace -f /tmp/spdk_nvmf_record.trace
~~~

## Adding New Tracepoints {#add_tracepoints}

SPDK applications and libraries provide several trace points. You can add new
tracepoints to the existing trace groups. For example, to add a new tracepoints
to the SPDK RDMA library (lib/nvmf/rdma.c) trace group TRACE_GROUP_NVMF_RDMA,
define the tracepoints and assigning them a unique ID using the SPDK_TPOINT_ID macro:

~~~c
#define	TRACE_GROUP_NVMF_RDMA	0x4
#define TRACE_RDMA_REQUEST_STATE_NEW	SPDK_TPOINT_ID(TRACE_GROUP_NVMF_RDMA, 0x0)
#define TRACE_RDMA_REQUEST_STATE_NEED_BUFFER	SPDK_TPOINT_ID(TRACE_GROUP_NVMF_RDMA, 0x1)
...
#define NEW_TRACE_POINT_NAME	SPDK_TPOINT_ID(TRACE_GROUP_NVMF_RDMA, UNIQUE_ID)
~~~

You also need to register the new trace points in the SPDK_TRACE_REGISTER_FN macro call
within the application/library using the spdk_trace_register_description function
as shown below:

~~~c
static void
nvmf_trace(void)
{
	spdk_trace_register_object(OBJECT_NVMF_RDMA_IO, 'r');
	spdk_trace_register_description("RDMA_REQ_NEW", TRACE_RDMA_REQUEST_STATE_NEW,
					OWNER_TYPE_NONE, OBJECT_NVMF_RDMA_IO, 1,
					SPDK_TRACE_ARG_TYPE_PTR, "qpair");
	spdk_trace_register_description("RDMA_REQ_NEED_BUFFER", TRACE_RDMA_REQUEST_STATE_NEED_BUFFER,
					OWNER_TYPE_NONE, OBJECT_NVMF_RDMA_IO, 0,
					SPDK_TRACE_ARG_TYPE_PTR, "qpair");
	...
}
SPDK_TRACE_REGISTER_FN(nvmf_trace, "nvmf_rdma", TRACE_GROUP_NVMF_RDMA)
~~~

Finally, use the spdk_trace_record function at the appropriate point in the
application/library to record the current trace state for the new trace points.
The following example shows the usage of the spdk_trace_record function to
record the current trace state of several tracepoints.

~~~c
	case RDMA_REQUEST_STATE_NEW:
		spdk_trace_record(TRACE_RDMA_REQUEST_STATE_NEW, 0, 0, (uintptr_t)rdma_req, (uintptr_t)rqpair);
		...
		break;
	case RDMA_REQUEST_STATE_NEED_BUFFER:
		spdk_trace_record(TRACE_RDMA_REQUEST_STATE_NEED_BUFFER, 0, 0, (uintptr_t)rdma_req, (uintptr_t)rqpair);
		...
		break;
	case RDMA_REQUEST_STATE_DATA_TRANSFER_TO_CONTROLLER_PENDING:
		spdk_trace_record(RDMA_REQUEST_STATE_DATA_TRANSFER_TO_CONTROLLER_PENDING, 0, 0,
			(uintptr_t)rdma_req, (uintptr_t)rqpair);
		...
~~~

All the tracing functions are documented in the [Tracepoint library documentation](https://spdk.io/doc/trace_8h.html)
