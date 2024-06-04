#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation.
#  All rights reserved.

#  Auto-generated code. DO NOT EDIT


def bdev_set_options(client, bdev_io_pool_size=None, bdev_io_cache_size=None, bdev_auto_examine=None, iobuf_small_cache_size=None,
                     iobuf_large_cache_size=None):
    """Set global parameters for the block device (bdev) subsystem. This RPC may only be called before SPDK subsystems have been
    initialized.
    Args:
        bdev_io_pool_size: Number of spdk_bdev_io structures in shared buffer pool
        bdev_io_cache_size: Maximum number of spdk_bdev_io structures cached per thread
        bdev_auto_examine: If set to false, the bdev layer will not examine every disks automatically
        iobuf_small_cache_size: Size of the small iobuf per thread cache
        iobuf_large_cache_size: Size of the large iobuf per thread cache
    """
    params = dict()
    if bdev_io_pool_size is not None:
        params['bdev_io_pool_size'] = bdev_io_pool_size
    if bdev_io_cache_size is not None:
        params['bdev_io_cache_size'] = bdev_io_cache_size
    if bdev_auto_examine is not None:
        params['bdev_auto_examine'] = bdev_auto_examine
    if iobuf_small_cache_size is not None:
        params['iobuf_small_cache_size'] = iobuf_small_cache_size
    if iobuf_large_cache_size is not None:
        params['iobuf_large_cache_size'] = iobuf_large_cache_size
    return client.call('bdev_set_options', params)


def bdev_examine(client, name):
    """Request that the bdev layer examines the given bdev for metadata and creates new bdevs if metadata is found. This is only
    necessary if auto_examine has been set to false using bdev_set_options. By default, auto_examine is true and bdev examination is
    automatic.
    Args:
        name: Block device name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_examine', params)


def bdev_wait_for_examine(client):
    """Report when all bdevs have been examined by every bdev module."""
    params = dict()

    return client.call('bdev_wait_for_examine', params)


def bdev_compress_create(client, base_bdev_name, pm_path, lb_size=None):
    """Create a new compress bdev on a given base bdev.
    Args:
        base_bdev_name: Name of the base bdev
        pm_path: Path to persistent memory
        lb_size: Compressed vol logical block size (512 or 4096)
    """
    params = dict()
    params['base_bdev_name'] = base_bdev_name
    params['pm_path'] = pm_path
    if lb_size is not None:
        params['lb_size'] = lb_size
    return client.call('bdev_compress_create', params)


def bdev_compress_delete(client, name):
    """Delete a compressed bdev.
    Args:
        name: Name of the compress bdev
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_compress_delete', params)


def bdev_compress_get_orphans(client, name=None):
    """Get a list of compressed volumes that are missing their pmem metadata.
    Args:
        name: Name of the compress bdev
    """
    params = dict()
    if name is not None:
        params['name'] = name
    return client.call('bdev_compress_get_orphans', params)


def bdev_crypto_create(client, base_bdev_name, name, crypto_pmd=None, key=None, cipher=None, key2=None, key_name=None):
    """Create a new crypto bdev on a given base bdev.
    Args:
        base_bdev_name: Name of the base bdev
        name: Name of the crypto vbdev to create
        crypto_pmd: Name of the crypto device driver. Obsolete, see accel_crypto_key_create
        key: Key in hex form. Obsolete, see accel_crypto_key_create
        cipher: Cipher to use, AES_CBC or AES_XTS (QAT and MLX5). Obsolete, see accel_crypto_key_create
        key2: 2nd key in hex form only required for cipher AET_XTS. Obsolete, see accel_crypto_key_create
        key_name: Name of the key created with accel_crypto_key_create
    """
    params = dict()
    params['base_bdev_name'] = base_bdev_name
    params['name'] = name
    if crypto_pmd is not None:
        params['crypto_pmd'] = crypto_pmd
    if key is not None:
        params['key'] = key
    if cipher is not None:
        params['cipher'] = cipher
    if key2 is not None:
        params['key2'] = key2
    if key_name is not None:
        params['key_name'] = key_name
    return client.call('bdev_crypto_create', params)


def bdev_crypto_delete(client, name):
    """Delete a crypto bdev.
    Args:
        name: Name of the crypto bdev
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_crypto_delete', params)


def bdev_ocf_create(client, name, mode, cache_bdev_name, core_bdev_name, cache_line_size=None):
    """Construct new OCF bdev. Command accepts cache mode that is going to be used. You can find more details about supported cache modes
    in the OCF documentation
    Args:
        name: Bdev name to use
        mode: OCF cache mode: wb, wt, pt, wa, wi, wo
        cache_bdev_name: Name of underlying cache bdev
        core_bdev_name: Name of underlying core bdev
        cache_line_size: OCF cache line size in KiB: 4, 8, 16, 32, 64
    """
    params = dict()
    params['name'] = name
    params['mode'] = mode
    params['cache_bdev_name'] = cache_bdev_name
    params['core_bdev_name'] = core_bdev_name
    if cache_line_size is not None:
        params['cache_line_size'] = cache_line_size
    return client.call('bdev_ocf_create', params)


def bdev_ocf_delete(client, name):
    """Delete the OCF bdev
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ocf_delete', params)


def bdev_ocf_get_stats(client, name):
    """Get statistics of chosen OCF block device.
    Args:
        name: Block device name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ocf_get_stats', params)


def bdev_ocf_reset_stats(client, name):
    """Reset statistics of chosen OCF block device.
    Args:
        name: Block device name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ocf_reset_stats', params)


def bdev_ocf_get_bdevs(client, name=None):
    """Get list of OCF devices including unregistered ones.
    Args:
        name: Name of OCF vbdev or name of cache device or name of core device
    """
    params = dict()
    if name is not None:
        params['name'] = name
    return client.call('bdev_ocf_get_bdevs', params)


def bdev_ocf_set_cache_mode(client, name, mode):
    """Set new cache mode on OCF bdev.
    Args:
        name: Bdev name
        mode: OCF cache mode: wb, wt, pt, wa, wi, wo
    """
    params = dict()
    params['name'] = name
    params['mode'] = mode
    return client.call('bdev_ocf_set_cache_mode', params)


def bdev_ocf_set_seqcutoff(client, name, policy, threshold=None, promotion_count=None):
    """Set sequential cutoff parameters on all cores for the given OCF cache device. A brief description of this functionality can be
    found in OpenCAS documentation.
    Args:
        name: Bdev name
        policy: Sequential cutoff policy: always, full, never
        threshold: Activation threshold in KiB
        promotion_count: Promotion request count
    """
    params = dict()
    params['name'] = name
    params['policy'] = policy
    if threshold is not None:
        params['threshold'] = threshold
    if promotion_count is not None:
        params['promotion_count'] = promotion_count
    return client.call('bdev_ocf_set_seqcutoff', params)


def bdev_ocf_flush_start(client, name):
    """Start flushing OCF cache device.
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ocf_flush_start', params)


def bdev_ocf_flush_status(client, name):
    """Get flush status of OCF cache device.
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ocf_flush_status', params)


def bdev_malloc_create(client, num_blocks, block_size, physical_block_size=None, name=None, uuid=None, optimal_io_boundary=None,
                       md_size=None, md_interleave=None, dif_type=None, dif_is_head_of_md=None):
    """Construct Malloc bdev
    Args:
        num_blocks: Number of blocks
        block_size: Data block size in bytes -must be multiple of 512
        physical_block_size: Physical block size of device; must be a power of 2 and at least 512
        name: Bdev name to use
        uuid: UUID of new bdev
        optimal_io_boundary: Split on optimal IO boundary, in number of blocks, default 0
        md_size: Metadata size for this bdev (0, 8, 16, 32, 64, or 128). Default is 0.
        md_interleave: Metadata location, interleaved if true, and separated if false. Default is false.
        dif_type: Protection information type. Parameter -md-size needs to be set along -dif-type. Default=0 - no protection.
        dif_is_head_of_md: Protection information is in the first 8 bytes of metadata. Default=false.
    """
    params = dict()
    params['num_blocks'] = num_blocks
    params['block_size'] = block_size
    if physical_block_size is not None:
        params['physical_block_size'] = physical_block_size
    if name is not None:
        params['name'] = name
    if uuid is not None:
        params['uuid'] = uuid
    if optimal_io_boundary is not None:
        params['optimal_io_boundary'] = optimal_io_boundary
    if md_size is not None:
        params['md_size'] = md_size
    if md_interleave is not None:
        params['md_interleave'] = md_interleave
    if dif_type is not None:
        params['dif_type'] = dif_type
    if dif_is_head_of_md is not None:
        params['dif_is_head_of_md'] = dif_is_head_of_md
    return client.call('bdev_malloc_create', params)


def bdev_malloc_delete(client, name):
    """Delete Malloc bdev
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_malloc_delete', params)


def bdev_null_create(client, num_blocks, block_size, name, physical_block_size=None, uuid=None, md_size=None, dif_type=None,
                     dif_is_head_of_md=None):
    """Construct Null
    Args:
        num_blocks: Number of blocks
        block_size: Block size in bytes
        name: Bdev name to use
        physical_block_size: physical block size of the device; data part size must be a power of 2 and at least 512
        uuid: UUID of new bdev
        md_size: Metadata size for this bdev. Default=0.
        dif_type: Protection information type. Parameter -md-size needs to be set along -dif-type. Default=0 - no protection.
        dif_is_head_of_md: Protection information is in the first 8 bytes of metadata. Default=false.
    """
    params = dict()
    params['num_blocks'] = num_blocks
    params['block_size'] = block_size
    params['name'] = name
    if physical_block_size is not None:
        params['physical_block_size'] = physical_block_size
    if uuid is not None:
        params['uuid'] = uuid
    if md_size is not None:
        params['md_size'] = md_size
    if dif_type is not None:
        params['dif_type'] = dif_type
    if dif_is_head_of_md is not None:
        params['dif_is_head_of_md'] = dif_is_head_of_md
    return client.call('bdev_null_create', params)


def bdev_null_delete(client, name):
    """Delete Null.
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_null_delete', params)


def bdev_null_resize(client, name, new_size):
    """Resize Null.
    Args:
        name: Bdev name
        new_size: Bdev new capacity in MiB
    """
    params = dict()
    params['name'] = name
    params['new_size'] = new_size
    return client.call('bdev_null_resize', params)


def bdev_raid_set_options(client, process_window_size_kb=None):
    """Set options for bdev raid
    Args:
        process_window_size_kb: Background process (e.g. rebuild) window size in KiB
    """
    params = dict()
    if process_window_size_kb is not None:
        params['process_window_size_kb'] = process_window_size_kb
    return client.call('bdev_raid_set_options', params)


def bdev_raid_get_bdevs(client, category):
    """This is used to list all the raid bdev details based on the input category requested. Category should be one of 'all', 'online',
    'configuring' or 'offline'. 'all' means all the raid bdevs whether they are online or configuring or offline. 'online' is the raid
    bdev which is registered with bdev layer. 'configuring' is the raid bdev which does not have full configuration discovered yet.
    'offline' is the raid bdev which is not registered with bdev as of now and it has encountered any error or user has requested to
    offline the raid bdev.
    Args:
        category: All or online or configuring or offline
    """
    params = dict()
    params['category'] = category
    return client.call('bdev_raid_get_bdevs', params)


def bdev_raid_create(client, name, raid_level, base_bdevs, strip_size_kb=None, uuid=None, superblock=None):
    """Constructs new RAID bdev.
    Args:
        name: RAID bdev name
        raid_level: RAID level
        base_bdevs: Base bdevs name, whitespace separated list in quotes
        strip_size_kb: Strip size in KB
        uuid: UUID for this RAID bdev
        superblock: If set, information about raid bdev will be stored in superblock on each base bdev (default: `false`)
    """
    params = dict()
    params['name'] = name
    params['raid_level'] = raid_level
    params['base_bdevs'] = base_bdevs
    if strip_size_kb is not None:
        params['strip_size_kb'] = strip_size_kb
    if uuid is not None:
        params['uuid'] = uuid
    if superblock is not None:
        params['superblock'] = superblock
    return client.call('bdev_raid_create', params)


def bdev_raid_delete(client, name):
    """Removes RAID bdev.
    Args:
        name: RAID bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_raid_delete', params)


def bdev_raid_add_base_bdev(client, base_bdev, raid_bdev):
    """Add base bdev to existing raid bdev
    Args:
        base_bdev: Base bdev name
        raid_bdev: Raid bdev name
    """
    params = dict()
    params['base_bdev'] = base_bdev
    params['raid_bdev'] = raid_bdev
    return client.call('bdev_raid_add_base_bdev', params)


def bdev_raid_remove_base_bdev(client, name):
    """Remove base bdev from existing raid bdev.
    Args:
        name: Base bdev name in RAID
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_raid_remove_base_bdev', params)


def bdev_aio_create(client, filename, name, block_size=None, readonly=None, fallocate=None):
    """Construct Linux AIO bdev.
    Args:
        filename: Path to device or file
        name: Bdev name to use
        block_size: Block size in bytes
        readonly: Set aio bdev as read-only
        fallocate: Enable UNMAP and WRITE ZEROES support. Intended only for testing purposes due to synchronous syscall.
    """
    params = dict()
    params['filename'] = filename
    params['name'] = name
    if block_size is not None:
        params['block_size'] = block_size
    if readonly is not None:
        params['readonly'] = readonly
    if fallocate is not None:
        params['fallocate'] = fallocate
    return client.call('bdev_aio_create', params)


def bdev_aio_rescan(client, name):
    """Rescan the size of Linux AIO bdev.
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_aio_rescan', params)


def bdev_aio_delete(client, name):
    """Delete Linux AIO bdev.
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_aio_delete', params)


def bdev_uring_create(client, filename, name, block_size=None, uuid=None):
    """Create a bdev with io_uring backend.
    Args:
        filename: Path to device or file (ex: /dev/nvme0n1)
        name: Name of bdev
        block_size: Block size of device (If omitted, get the block size from the file)
        uuid: UUID of new bdev
    """
    params = dict()
    params['filename'] = filename
    params['name'] = name
    if block_size is not None:
        params['block_size'] = block_size
    if uuid is not None:
        params['uuid'] = uuid
    return client.call('bdev_uring_create', params)


def bdev_uring_rescan(client, name):
    """Rescan a Linux URING block device.
    Args:
        name: Name of uring bdev to rescan
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_uring_rescan', params)


def bdev_uring_delete(client, name):
    """Remove a uring bdev.
    Args:
        name: Name of uring bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_uring_delete', params)


def bdev_xnvme_create(client, filename, name, io_mechanism, conserve_cpu=None):
    """Create xnvme bdev. This bdev type redirects all IO to its underlying backend.
    Args:
        filename: Path to device or file (ex: /dev/nvme0n1)
        name: Name of xNVMe bdev to create
        io_mechanism: IO mechanism to use (ex: libaio, io_uring, io_uring_cmd, etc.)
        conserve_cpu: Whether or not to conserve CPU when polling (default: false)
    """
    params = dict()
    params['filename'] = filename
    params['name'] = name
    params['io_mechanism'] = io_mechanism
    if conserve_cpu is not None:
        params['conserve_cpu'] = conserve_cpu
    return client.call('bdev_xnvme_create', params)


def bdev_xnvme_delete(client, name):
    """Delete xnvme bdev.
    Args:
        name: Name of xnvme bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_xnvme_delete', params)


def bdev_nvme_set_options(client, action_on_timeout=None, timeout_us=None, timeout_admin_us=None, keep_alive_timeout_ms=None,
                          arbitration_burst=None, low_priority_weight=None, medium_priority_weight=None, high_priority_weight=None,
                          nvme_adminq_poll_period_us=None, nvme_ioq_poll_period_us=None, io_queue_requests=None, delay_cmd_submit=None,
                          transport_retry_count=None, bdev_retry_count=None, transport_ack_timeout=None, ctrlr_loss_timeout_sec=None,
                          reconnect_delay_sec=None, fast_io_fail_timeout_sec=None, disable_auto_failback=None, generate_uuids=None,
                          transport_tos=None, nvme_error_stat=None, rdma_srq_size=None, io_path_stat=None, allow_accel_sequence=None,
                          rdma_max_cq_size=None, rdma_cm_event_timeout_ms=None, dhchap_digests=None, dhchap_dhgroups=None):
    """Set global parameters for all bdev NVMe. This RPC may only be called before SPDK subsystems have been initialized or any bdev NVMe
    has been created.
    Args:
        action_on_timeout: Action to take on command time out. Valid values are: none, reset, abort
        timeout_us: Timeout for each command, in microseconds. If 0, don't track timeouts
        timeout_admin_us: Timeout for each admin command, in microseconds. If 0, treat same as io timeouts
        keep_alive_timeout_ms: Keep alive timeout period in millisecond, default is 10s
        arbitration_burst: The value is expressed as a power of two, a value of 111b indicates no limit
        low_priority_weight: The maximum number of commands that the controller may launch at one time from a low
        medium_priority_weight: The maximum number of commands that the controller may launch at one time from a medium priority queue
        high_priority_weight: The maximum number of commands that the controller may launch at one time from a high priority queue
        nvme_adminq_poll_period_us: How often the admin queue is polled for asynchronous events in microseconds
        nvme_ioq_poll_period_us: How often I/O queues are polled for completions, in microseconds. Default: 0 (as fast as possible).
        io_queue_requests: The number of requests allocated for each NVMe I/O queue. Default: 512.
        delay_cmd_submit: Enable delaying NVMe command submission to allow batching of multiple commands. Default: true.
        transport_retry_count: The number of attempts per I/O in the transport layer before an I/O fails.
        bdev_retry_count: The number of attempts per I/O in the bdev layer before an I/O fails. -1 means infinite retries.
        transport_ack_timeout: Time to wait ack until retransmission for RDMA or connection close for TCP. Range 0-31 where 0 means use
        default.
        ctrlr_loss_timeout_sec: Time to wait until ctrlr is reconnected before deleting ctrlr. -1 means infinite reconnects. 0 means no
        reconnect.
        reconnect_delay_sec: Time to delay a reconnect trial. 0 means no reconnect.
        fast_io_fail_timeout_sec: Time to wait until ctrlr is reconnected before failing I/O to ctrlr. 0 means no such timeout.
        disable_auto_failback: Disable automatic failback. The RPC bdev_nvme_set_preferred_path can be used to do manual failback.
        generate_uuids: Enable generation of UUIDs for NVMe bdevs that do not provide this value themselves.
        transport_tos: IPv4 Type of Service value. Only applicable for RDMA transport. Default: 0 (no TOS is applied).
        nvme_error_stat: Enable collecting NVMe error counts.
        rdma_srq_size: Set the size of a shared rdma receive queue. Default: 0 (disabled).
        io_path_stat: Enable collecting I/O stat of each nvme bdev io path. Default: `false`.
        allow_accel_sequence: Allow NVMe bdevs to advertise support for accel sequences if the controller also supports them. Default:
        `false`.
        rdma_max_cq_size: Set the maximum size of a rdma completion queue. Default: 0 (unlimited)
        rdma_cm_event_timeout_ms: Time to wait for RDMA CM events. Default: 0 (0 means using default value of driver).
        dhchap_digests: List of allowed DH-HMAC-CHAP digests.
        dhchap_dhgroups: List of allowed DH-HMAC-CHAP DH groups.
    """
    params = dict()
    if action_on_timeout is not None:
        params['action_on_timeout'] = action_on_timeout
    if timeout_us is not None:
        params['timeout_us'] = timeout_us
    if timeout_admin_us is not None:
        params['timeout_admin_us'] = timeout_admin_us
    if keep_alive_timeout_ms is not None:
        params['keep_alive_timeout_ms'] = keep_alive_timeout_ms
    if arbitration_burst is not None:
        params['arbitration_burst'] = arbitration_burst
    if low_priority_weight is not None:
        params['low_priority_weight'] = low_priority_weight
    if medium_priority_weight is not None:
        params['medium_priority_weight'] = medium_priority_weight
    if high_priority_weight is not None:
        params['high_priority_weight'] = high_priority_weight
    if nvme_adminq_poll_period_us is not None:
        params['nvme_adminq_poll_period_us'] = nvme_adminq_poll_period_us
    if nvme_ioq_poll_period_us is not None:
        params['nvme_ioq_poll_period_us'] = nvme_ioq_poll_period_us
    if io_queue_requests is not None:
        params['io_queue_requests'] = io_queue_requests
    if delay_cmd_submit is not None:
        params['delay_cmd_submit'] = delay_cmd_submit
    if transport_retry_count is not None:
        params['transport_retry_count'] = transport_retry_count
    if bdev_retry_count is not None:
        params['bdev_retry_count'] = bdev_retry_count
    if transport_ack_timeout is not None:
        params['transport_ack_timeout'] = transport_ack_timeout
    if ctrlr_loss_timeout_sec is not None:
        params['ctrlr_loss_timeout_sec'] = ctrlr_loss_timeout_sec
    if reconnect_delay_sec is not None:
        params['reconnect_delay_sec'] = reconnect_delay_sec
    if fast_io_fail_timeout_sec is not None:
        params['fast_io_fail_timeout_sec'] = fast_io_fail_timeout_sec
    if disable_auto_failback is not None:
        params['disable_auto_failback'] = disable_auto_failback
    if generate_uuids is not None:
        params['generate_uuids'] = generate_uuids
    if transport_tos is not None:
        params['transport_tos'] = transport_tos
    if nvme_error_stat is not None:
        params['nvme_error_stat'] = nvme_error_stat
    if rdma_srq_size is not None:
        params['rdma_srq_size'] = rdma_srq_size
    if io_path_stat is not None:
        params['io_path_stat'] = io_path_stat
    if allow_accel_sequence is not None:
        params['allow_accel_sequence'] = allow_accel_sequence
    if rdma_max_cq_size is not None:
        params['rdma_max_cq_size'] = rdma_max_cq_size
    if rdma_cm_event_timeout_ms is not None:
        params['rdma_cm_event_timeout_ms'] = rdma_cm_event_timeout_ms
    if dhchap_digests is not None:
        params['dhchap_digests'] = dhchap_digests
    if dhchap_dhgroups is not None:
        params['dhchap_dhgroups'] = dhchap_dhgroups
    return client.call('bdev_nvme_set_options', params)


def bdev_nvme_set_hotplug(client, enable, period_us=None):
    """Change settings of the NVMe hotplug feature. If enabled, PCIe NVMe bdevs will be automatically discovered on insertion and deleted
    on removal.
    Args:
        enable: True to enable, false to disable
        period_us: How often to poll for hot-insert and hot-remove events. Values: 0 - reset/use default or 1 to 10000000.
    """
    params = dict()
    params['enable'] = enable
    if period_us is not None:
        params['period_us'] = period_us
    return client.call('bdev_nvme_set_hotplug', params)


def bdev_nvme_attach_controller(client, name, trtype, traddr, adrfam=None, trsvcid=None, priority=None, subnqn=None, hostnqn=None,
                                hostaddr=None, hostsvcid=None, prchk_reftag=None, prchk_guard=None, hdgst=None, ddgst=None,
                                fabrics_connect_timeout_us=None, multipath=None, num_io_queues=None, ctrlr_loss_timeout_sec=None,
                                reconnect_delay_sec=None, fast_io_fail_timeout_sec=None, psk=None, max_bdevs=None, dhchap_key=None,
                                dhchap_ctrlr_key=None):
    """Construct NVMe bdev. This RPC can also be used to add additional paths to an existing controller to enable multipathing. This is
    done by specifying the name parameter as an existing controller. When adding an additional path, the hostnqn, hostsvcid, hostaddr,
    prchk_reftag, and prchk_guard_arguments must not be specified and are assumed to have the same value as the existing path.
    Args:
        name: Name of the NVMe controller, prefix for each bdev name
        trtype: NVMe-oF target trtype: 'PCIe', 'RDMA', 'FC', 'TCP'
        traddr: NVMe-oF target address: PCI BDF or IP address
        adrfam: NVMe-oF target adrfam: ipv4, ipv6, ib, fc, intra_host
        trsvcid: NVMe-oF target trsvcid: port number for IP-based addresses
        priority: Transport connection priority. Supported by TCP transport with POSIX sock module (see socket(7) man page).
        subnqn: NVMe-oF target subnqn
        hostnqn: NVMe-oF target hostnqn
        hostaddr: NVMe-oF host address: ip address
        hostsvcid: NVMe-oF host trsvcid: port number
        prchk_reftag: Enable checking of PI reference tag for I/O processing
        prchk_guard: Enable checking of PI guard for I/O processing
        hdgst: Enable TCP header digest
        ddgst: Enable TCP data digest
        fabrics_connect_timeout_us: Timeout for fabrics connect (in microseconds)
        multipath: Multipathing behavior: disable, failover, multipath. Default is failover.
        num_io_queues: The number of IO queues to request during initialization. Range: (0, UINT16_MAX + 1], Default is 1024.
        ctrlr_loss_timeout_sec: Time to wait until ctrlr is reconnected before deleting ctrlr.  -1 means infinite reconnects. 0 means no
        reconnect.
        reconnect_delay_sec: Time to delay a reconnect retry. If ctrlr_loss_timeout_sec is zero, reconnect_delay_sec has to be zero. If
        ctrlr_loss_timeout_sec is -1, reconnect_delay_sec has to be non-zero. If ctrlr_loss_timeout_sec is not -1 or zero, reconnect_sec
        has to be non-zero and less than ctrlr_loss_timeout_sec.
        fast_io_fail_timeout_sec: Time to wait until ctrlr is reconnected before failing I/O to ctrlr. 0 means no such timeout. If
        fast_io_fail_timeout_sec is not zero, it has to be not less than reconnect_delay_sec and less than ctrlr_loss_timeout_sec if
        ctrlr_loss_timeout_sec is not -1.
        psk:  Name of the pre-shared key to be used for TLS (Enables SSL socket implementation for TCP)
        max_bdevs: The size of the name array for newly created bdevs. Default is 128.
        dhchap_key: DH-HMAC-CHAP key name.
        dhchap_ctrlr_key: DH-HMAC-CHAP controller key name.
    """
    params = dict()
    params['name'] = name
    params['trtype'] = trtype
    params['traddr'] = traddr
    if adrfam is not None:
        params['adrfam'] = adrfam
    if trsvcid is not None:
        params['trsvcid'] = trsvcid
    if priority is not None:
        params['priority'] = priority
    if subnqn is not None:
        params['subnqn'] = subnqn
    if hostnqn is not None:
        params['hostnqn'] = hostnqn
    if hostaddr is not None:
        params['hostaddr'] = hostaddr
    if hostsvcid is not None:
        params['hostsvcid'] = hostsvcid
    if prchk_reftag is not None:
        params['prchk_reftag'] = prchk_reftag
    if prchk_guard is not None:
        params['prchk_guard'] = prchk_guard
    if hdgst is not None:
        params['hdgst'] = hdgst
    if ddgst is not None:
        params['ddgst'] = ddgst
    if fabrics_connect_timeout_us is not None:
        params['fabrics_connect_timeout_us'] = fabrics_connect_timeout_us
    if multipath is not None:
        params['multipath'] = multipath
    if num_io_queues is not None:
        params['num_io_queues'] = num_io_queues
    if ctrlr_loss_timeout_sec is not None:
        params['ctrlr_loss_timeout_sec'] = ctrlr_loss_timeout_sec
    if reconnect_delay_sec is not None:
        params['reconnect_delay_sec'] = reconnect_delay_sec
    if fast_io_fail_timeout_sec is not None:
        params['fast_io_fail_timeout_sec'] = fast_io_fail_timeout_sec
    if psk is not None:
        params['psk'] = psk
    if max_bdevs is not None:
        params['max_bdevs'] = max_bdevs
    if dhchap_key is not None:
        params['dhchap_key'] = dhchap_key
    if dhchap_ctrlr_key is not None:
        params['dhchap_ctrlr_key'] = dhchap_ctrlr_key
    return client.call('bdev_nvme_attach_controller', params)


def bdev_nvme_detach_controller(client, name, trtype=None, traddr=None, adrfam=None, trsvcid=None, subnqn=None, hostaddr=None,
                                hostsvcid=None):
    """Detach NVMe controller and delete any associated bdevs. Optionally, If all of the transport ID options are specified, only remove
    that transport path from the specified controller. If that is the only available path for the controller, this will also result in
    the controller being detached and the associated bdevs being deleted.
    Args:
        name: Controller name
        trtype: NVMe-oF target trtype: rdma or tcp
        traddr: NVMe-oF target address: ip or BDF
        adrfam: NVMe-oF target adrfam: ipv4, ipv6, ib, fc, intra_host
        trsvcid: NVMe-oF target trsvcid: port number
        subnqn: NVMe-oF target subnqn
        hostaddr: NVMe-oF host address: ip
        hostsvcid: NVMe-oF host svcid: port number
    """
    params = dict()
    params['name'] = name
    if trtype is not None:
        params['trtype'] = trtype
    if traddr is not None:
        params['traddr'] = traddr
    if adrfam is not None:
        params['adrfam'] = adrfam
    if trsvcid is not None:
        params['trsvcid'] = trsvcid
    if subnqn is not None:
        params['subnqn'] = subnqn
    if hostaddr is not None:
        params['hostaddr'] = hostaddr
    if hostsvcid is not None:
        params['hostsvcid'] = hostsvcid
    return client.call('bdev_nvme_detach_controller', params)


def bdev_nvme_reset_controller(client, name, cntlid=None):
    """For non NVMe multipath, reset an NVMe controller whose name is given by the name parameter.
    Args:
        name: NVMe controller name (or NVMe bdev controller name for multipath)
        cntlid: NVMe controller ID (used as NVMe controller name for multipath)
    """
    params = dict()
    params['name'] = name
    if cntlid is not None:
        params['cntlid'] = cntlid
    return client.call('bdev_nvme_reset_controller', params)


def bdev_nvme_enable_controller(client, name, cntlid=None):
    """For non NVMe multipath, enable an NVMe controller whose name is given by the name parameter.
    Args:
        name: NVMe controller name (or NVMe bdev controller name for multipath)
        cntlid: NVMe controller ID (used as NVMe controller name for multipath)
    """
    params = dict()
    params['name'] = name
    if cntlid is not None:
        params['cntlid'] = cntlid
    return client.call('bdev_nvme_enable_controller', params)


def bdev_nvme_disable_controller(client, name, cntlid=None):
    """For non NVMe multipath, disable an NVMe controller whose name is given by the name parameter.
    Args:
        name: NVMe controller name (or NVMe bdev controller name for multipath)
        cntlid: NVMe controller ID (used as NVMe controller name for multipath)
    """
    params = dict()
    params['name'] = name
    if cntlid is not None:
        params['cntlid'] = cntlid
    return client.call('bdev_nvme_disable_controller', params)


def bdev_nvme_start_discovery(client, name, trtype, traddr, adrfam=None, trsvcid=None, hostnqn=None, wait_for_attach=None,
                              ctrlr_loss_timeout_sec=None, reconnect_delay_sec=None, fast_io_fail_timeout_sec=None, attach_timeout_ms=None):
    """Start a discovery service for the discovery subsystem of the specified transport ID.
    Args:
        name: Prefix for NVMe controllers
        trtype: NVMe-oF target trtype: rdma or tcp
        traddr: NVMe-oF target address: ip
        adrfam: NVMe-oF target adrfam: ipv4, ipv6
        trsvcid: NVMe-oF target trsvcid: port number
        hostnqn: NVMe-oF target hostnqn
        wait_for_attach: Wait to complete until all discovered NVM subsystems are attached
        ctrlr_loss_timeout_sec: Time to wait until ctrlr is reconnected before deleting ctrlr. -1 means infinite reconnects. 0 means no
        reconnect.
        reconnect_delay_sec: Time to delay a reconnect trial. 0 means no reconnect.
        fast_io_fail_timeout_sec: Time to wait until ctrlr is reconnected before failing I/O to ctrlr. 0 means no such timeout.
        attach_timeout_ms: Time to wait until the discovery and all discovered NVM subsystems are attached
    """
    params = dict()
    params['name'] = name
    params['trtype'] = trtype
    params['traddr'] = traddr
    if adrfam is not None:
        params['adrfam'] = adrfam
    if trsvcid is not None:
        params['trsvcid'] = trsvcid
    if hostnqn is not None:
        params['hostnqn'] = hostnqn
    if wait_for_attach is not None:
        params['wait_for_attach'] = wait_for_attach
    if ctrlr_loss_timeout_sec is not None:
        params['ctrlr_loss_timeout_sec'] = ctrlr_loss_timeout_sec
    if reconnect_delay_sec is not None:
        params['reconnect_delay_sec'] = reconnect_delay_sec
    if fast_io_fail_timeout_sec is not None:
        params['fast_io_fail_timeout_sec'] = fast_io_fail_timeout_sec
    if attach_timeout_ms is not None:
        params['attach_timeout_ms'] = attach_timeout_ms
    return client.call('bdev_nvme_start_discovery', params)


def bdev_nvme_stop_discovery(client, name):
    """Stop a discovery service. This includes detaching any controllers that were discovered via the service that is being stopped.
    Args:
        name: Name of service to stop
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_nvme_stop_discovery', params)


def bdev_nvme_get_discovery_info(client):
    """Get information about the discovery service."""
    params = dict()

    return client.call('bdev_nvme_get_discovery_info', params)


def bdev_nvme_get_io_paths(client, name=None):
    """Display all or the specified NVMe bdev's active I/O paths.
    Args:
        name: Name of the NVMe bdev
    """
    params = dict()
    if name is not None:
        params['name'] = name
    return client.call('bdev_nvme_get_io_paths', params)


def bdev_nvme_set_preferred_path(client, name, cntlid):
    """Set the preferred I/O path for an NVMe bdev in multipath mode.
    Args:
        name: Name of the NVMe bdev
        cntlid: NVMe-oF controller ID
    """
    params = dict()
    params['name'] = name
    params['cntlid'] = cntlid
    return client.call('bdev_nvme_set_preferred_path', params)


def bdev_nvme_set_multipath_policy(client, name, policy, selector=None, rr_min_io=None):
    """Set multipath policy of the NVMe bdev in multipath mode or set multipath selector for active-active multipath policy.
    Args:
        name: Name of the NVMe bdev
        policy: Multipath policy: active_active or active_passive
        selector: Multipath selector: round_robin or queue_depth, used in active-active mode. Default is round_robin
        rr_min_io: Number of I/Os routed to current io path before switching to another for round-robin selector. The min value is 1.
    """
    params = dict()
    params['name'] = name
    params['policy'] = policy
    if selector is not None:
        params['selector'] = selector
    if rr_min_io is not None:
        params['rr_min_io'] = rr_min_io
    return client.call('bdev_nvme_set_multipath_policy', params)


def bdev_nvme_get_path_iostat(client, name):
    """Get I/O statistics for IO paths of the block device. Call RPC bdev_nvme_set_options to set enable_io_path_stat true before using
    this RPC.
    Args:
        name: Name of the NVMe bdev
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_nvme_get_path_iostat', params)


def bdev_nvme_cuse_register(client, name):
    """Register CUSE device on NVMe controller. This feature is considered as experimental.
    Args:
        name: Name of the NVMe controller
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_nvme_cuse_register', params)


def bdev_nvme_cuse_unregister(client, name):
    """Unregister CUSE device on NVMe controller. This feature is considered as experimental.
    Args:
        name: Name of the NVMe controller
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_nvme_cuse_unregister', params)


def bdev_zone_block_create(client, name, base_bdev, zone_capacity, optimal_open_zones):
    """Creates a virtual zone device on top of existing non-zoned bdev.
    Args:
        name: Name of the Zone device
        base_bdev: Name of the Base bdev
        zone_capacity: Zone capacity in blocks
        optimal_open_zones: Number of zones required to reach optimal write speed
    """
    params = dict()
    params['name'] = name
    params['base_bdev'] = base_bdev
    params['zone_capacity'] = zone_capacity
    params['optimal_open_zones'] = optimal_open_zones
    return client.call('bdev_zone_block_create', params)


def bdev_zone_block_delete(client, name):
    """Deletes a virtual zone device.
    Args:
        name: Name of the Zone device
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_zone_block_delete', params)


def bdev_rbd_register_cluster(client, name=None, user_id=None, config_param=None, config_file=None, key_file=None, core_mask=None):
    """This method is available only if SPDK was build with Ceph RBD support.
    Args:
        name: Registered Rados cluster object name
        user_id: Ceph ID (i.e. admin, not client.admin)
        config_param: Explicit librados configuration
        config_file: File path of libraodos configuration file
        key_file: File path of libraodos key file
        core_mask: core mask for librbd IO context threads
    """
    params = dict()
    if name is not None:
        params['name'] = name
    if user_id is not None:
        params['user_id'] = user_id
    if config_param is not None:
        params['config_param'] = config_param
    if config_file is not None:
        params['config_file'] = config_file
    if key_file is not None:
        params['key_file'] = key_file
    if core_mask is not None:
        params['core_mask'] = core_mask
    return client.call('bdev_rbd_register_cluster', params)


def bdev_rbd_unregister_cluster(client, name):
    """This method is available only if SPDK was build with Ceph RBD support. If there is still rbd bdev using this cluster, the
    unregisteration operation will fail.
    Args:
        name: Rados cluster object name to unregister
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_rbd_unregister_cluster', params)


def bdev_rbd_get_clusters_info(client, name=None):
    """This method is available only if SPDK was build with Ceph RBD support.
    Args:
        name: Rados cluster object name to query (if omitted, query all clusters)
    """
    params = dict()
    if name is not None:
        params['name'] = name
    return client.call('bdev_rbd_get_clusters_info', params)


def bdev_rbd_create(client, pool_name, rbd_name, block_size, name=None, user_id=None, config=None, cluster_name=None, uuid=None):
    """Create Ceph RBD bdev
    Args:
        pool_name: Ceph RBD pool name
        rbd_name: Ceph RBD image name
        block_size: Block size of RBD volume
        name: Name of block device
        user_id: Ceph user name (i.e. admin, not client.admin)
        config: Explicit librados configuration
        cluster_name: Rados cluster object name created in this module.
        uuid: UUID of new bdev
    """
    params = dict()
    params['pool_name'] = pool_name
    params['rbd_name'] = rbd_name
    params['block_size'] = block_size
    if name is not None:
        params['name'] = name
    if user_id is not None:
        params['user_id'] = user_id
    if config is not None:
        params['config'] = config
    if cluster_name is not None:
        params['cluster_name'] = cluster_name
    if uuid is not None:
        params['uuid'] = uuid
    return client.call('bdev_rbd_create', params)


def bdev_rbd_delete(client, name):
    """Delete Ceph RBD bdev
    Args:
        name: Name of rbd bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_rbd_delete', params)


def bdev_rbd_resize(client, name, new_size):
    """Resize Ceph RBD bdev
    Args:
        name: Name of rbd bdev to resize
        new_size: New bdev size of resize operation. The unit is MiB
    """
    params = dict()
    params['name'] = name
    params['new_size'] = new_size
    return client.call('bdev_rbd_resize', params)


def bdev_error_create(client, base_name, uuid=None):
    """Construct error bdev.
    Args:
        base_name: Base bdev name
        uuid: UUID for this bdev
    """
    params = dict()
    params['base_name'] = base_name
    if uuid is not None:
        params['uuid'] = uuid
    return client.call('bdev_error_create', params)


def bdev_delay_create(client, base_bdev_name, name, avg_read_latency, p99_read_latency, avg_write_latency, p99_write_latency, uuid=None):
    """Create delay bdev. This bdev type redirects all IO to it's base bdev and inserts a delay on the completion path to create an
    artificial drive latency. All latency values supplied to this bdev should be in microseconds.
    Args:
        base_bdev_name: Name of the existing bdev
        name: Name of block device
        avg_read_latency: Average read latency (us). Complete 99% of read ops with this delay
        p99_read_latency: p99 read latency (us). Complete 1% of read ops with this delay
        avg_write_latency: average write latency (us). Complete 99% of write ops with this delay
        p99_write_latency: p99 write latency (us). Complete 1% of write ops with this delay
        uuid: UUID of block device
    """
    params = dict()
    params['base_bdev_name'] = base_bdev_name
    params['name'] = name
    params['avg_read_latency'] = avg_read_latency
    params['p99_read_latency'] = p99_read_latency
    params['avg_write_latency'] = avg_write_latency
    params['p99_write_latency'] = p99_write_latency
    if uuid is not None:
        params['uuid'] = uuid
    return client.call('bdev_delay_create', params)


def bdev_delay_delete(client, name):
    """Delete delay bdev.
    Args:
        name: Name of delay bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_delay_delete', params)


def bdev_delay_update_latency(client, delay_bdev_name, latency_type, latency_us):
    """Update a target latency value associated with a given delay bdev. Any currently outstanding I/O will be completed with the old
    latency.
    Args:
        delay_bdev_name: Name of the delay bdev
        latency_type: One of: avg_read, avg_write, p99_read, p99_write
        latency_us: The new latency value in microseconds
    """
    params = dict()
    params['delay_bdev_name'] = delay_bdev_name
    params['latency_type'] = latency_type
    params['latency_us'] = latency_us
    return client.call('bdev_delay_update_latency', params)


def bdev_error_delete(client, name):
    """Delete error bdev
    Args:
        name:  Name of error bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_error_delete', params)


def bdev_iscsi_set_options(client, timeout_sec=None):
    """This RPC can be called at any time, but the new value will only take effect for new iSCSI bdevs.
    Args:
        timeout_sec: Timeout for command, in seconds, if 0, don't track timeout
    """
    params = dict()
    if timeout_sec is not None:
        params['timeout_sec'] = timeout_sec
    return client.call('bdev_iscsi_set_options', params)


def bdev_iscsi_create(client, name, url, initiator_iqn):
    """Connect to iSCSI target and create bdev backed by this connection.
    Args:
        name: Name of block device
        url: iSCSI resource URI
        initiator_iqn: IQN name used during connection
    """
    params = dict()
    params['name'] = name
    params['url'] = url
    params['initiator_iqn'] = initiator_iqn
    return client.call('bdev_iscsi_create', params)


def bdev_iscsi_delete(client, name):
    """Delete iSCSI bdev and terminate connection to target.
    Args:
        name: Name of iSCSI bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_iscsi_delete', params)


def bdev_passthru_create(client, base_bdev_name, name, uuid=None):
    """Create passthru bdev. This bdev type redirects all IO to it's base bdev. It has no other purpose than being an example and a
    starting point in development of new bdev type.
    Args:
        base_bdev_name: Name of the existing bdev
        name: Name of block device
        uuid: UUID of new bdev
    """
    params = dict()
    params['base_bdev_name'] = base_bdev_name
    params['name'] = name
    if uuid is not None:
        params['uuid'] = uuid
    return client.call('bdev_passthru_create', params)


def bdev_passthru_delete(client, name):
    """Delete passthru bdev.
    Args:
        name: Name of pass through bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_passthru_delete', params)


def bdev_opal_create(client, nvme_ctrlr_name, nsid, locking_range_id, range_start, range_length, password):
    """This is used to create an OPAL virtual bdev.
    Args:
        nvme_ctrlr_name: Name of nvme ctrlr that supports OPAL
        nsid: Namespace ID of nvme ctrlr
        locking_range_id: OPAL locking range ID corresponding to this virtual bdev
        range_start: Start address of this locking range
        range_length: Locking range length
        password: admin password of OPAL
    """
    params = dict()
    params['nvme_ctrlr_name'] = nvme_ctrlr_name
    params['nsid'] = nsid
    params['locking_range_id'] = locking_range_id
    params['range_start'] = range_start
    params['range_length'] = range_length
    params['password'] = password
    return client.call('bdev_opal_create', params)


def bdev_opal_get_info(client, bdev_name, password):
    """This is used to get information of a given OPAL bdev.
    Args:
        bdev_name: name of OPAL vbdev to get info
        password: admin password
    """
    params = dict()
    params['bdev_name'] = bdev_name
    params['password'] = password
    return client.call('bdev_opal_get_info', params)


def bdev_opal_delete(client, bdev_name, password):
    """This is used to delete OPAL vbdev.
    Args:
        bdev_name: Name of OPAL vbdev to delete
        password: Admin password of base nvme bdev
    """
    params = dict()
    params['bdev_name'] = bdev_name
    params['password'] = password
    return client.call('bdev_opal_delete', params)


def bdev_opal_new_user(client, bdev_name, admin_password, user_id, user_password):
    """This enables a new user to the specified opal bdev so that the user can lock/unlock the bdev. Recalling this for the same opal
    bdev, only the newest user will have the privilege.
    Args:
        bdev_name: Name of OPAL vbdev
        admin_password: Admin password
        user_id: ID of the user who will be added to this opal bdev
        user_password: Password set for this user
    """
    params = dict()
    params['bdev_name'] = bdev_name
    params['admin_password'] = admin_password
    params['user_id'] = user_id
    params['user_password'] = user_password
    return client.call('bdev_opal_new_user', params)


def bdev_opal_set_lock_state(client, bdev_name, user_id, password, lock_state):
    """This is used to lock/unlock specific opal bdev providing user ID and password.
    Args:
        bdev_name: Name of OPAL vbdev
        user_id: ID of the user who will set lock state
        password: Password of the user
        lock_state: Lock state to set
    """
    params = dict()
    params['bdev_name'] = bdev_name
    params['user_id'] = user_id
    params['password'] = password
    params['lock_state'] = lock_state
    return client.call('bdev_opal_set_lock_state', params)


def bdev_split_create(client, base_bdev, split_count, split_size_mb=None):
    """Create split block devices from a base bdev.
    Args:
        base_bdev: Base bdev name to split
        split_count: Number of splits bdevs to create
        split_size_mb: Size of each split volume in MiB
    """
    params = dict()
    params['base_bdev'] = base_bdev
    params['split_count'] = split_count
    if split_size_mb is not None:
        params['split_size_mb'] = split_size_mb
    return client.call('bdev_split_create', params)


def bdev_split_delete(client, base_bdev):
    """This is used to remove the split vbdevs.
    Args:
        base_bdev: Name of previously split bdev
    """
    params = dict()
    params['base_bdev'] = base_bdev
    return client.call('bdev_split_delete', params)


def bdev_ftl_create(client, name, base_bdev, cache, core_mask=None, fast_shutdown=None, uuid=None, overprovisioning=None,
                    l2p_dram_limit=None):
    """Create FTL bdev.
    Args:
        name: Name of the bdev
        base_bdev: Name of the base device
        cache: Name of the cache device
        core_mask: CPU core(s) possible for placement of the ftl core thread, application main thread by default
        fast_shutdown: When set FTL will minimize persisted data on target application shutdown and rely on shared memory during next load
        uuid: UUID of restored bdev (not applicable when creating new instance)
        overprovisioning: Percentage of base device used for relocation, 20% by default
        l2p_dram_limit: l2p size that could reside in DRAM; default 2048
    """
    params = dict()
    params['name'] = name
    params['base_bdev'] = base_bdev
    params['cache'] = cache
    if core_mask is not None:
        params['core_mask'] = core_mask
    if fast_shutdown is not None:
        params['fast_shutdown'] = fast_shutdown
    if uuid is not None:
        params['uuid'] = uuid
    if overprovisioning is not None:
        params['overprovisioning'] = overprovisioning
    if l2p_dram_limit is not None:
        params['l2p_dram_limit'] = l2p_dram_limit
    return client.call('bdev_ftl_create', params)


def bdev_ftl_load(client, name, base_bdev, cache, core_mask=None, fast_shutdown=None, uuid=None, overprovisioning=None,
                  l2p_dram_limit=None):
    """Loads FTL bdev.
    Args:
        name: Bdev name
        base_bdev: Name of the base device
        cache: Name of the cache device
        core_mask: CPU core(s) possible for placement of the ftl core thread, application main thread by default
        fast_shutdown: When set FTL will minimize persisted data on target application shutdown and rely on shared memory during next load
        uuid: UUID of restored bdev
        overprovisioning: Percentage of base device used for relocation, 20% by default
        l2p_dram_limit: l2p size that could reside in DRAM; default 2048
    """
    params = dict()
    params['name'] = name
    params['base_bdev'] = base_bdev
    params['cache'] = cache
    if core_mask is not None:
        params['core_mask'] = core_mask
    if fast_shutdown is not None:
        params['fast_shutdown'] = fast_shutdown
    if uuid is not None:
        params['uuid'] = uuid
    if overprovisioning is not None:
        params['overprovisioning'] = overprovisioning
    if l2p_dram_limit is not None:
        params['l2p_dram_limit'] = l2p_dram_limit
    return client.call('bdev_ftl_load', params)


def bdev_ftl_unload(client, name, fast_shutdown=None):
    """Unloads FTL bdev.
    Args:
        name: Bdev name
        fast_shutdown: When set FTL will minimize persisted data during deletion and rely on shared memory during next load
    """
    params = dict()
    params['name'] = name
    if fast_shutdown is not None:
        params['fast_shutdown'] = fast_shutdown
    return client.call('bdev_ftl_unload', params)


def bdev_ftl_delete(client, name, fast_shutdown=None):
    """Delete FTL bdev.
    Args:
        name: Bdev name
        fast_shutdown: When set FTL will minimize persisted data during deletion and rely on shared memory during next load
    """
    params = dict()
    params['name'] = name
    if fast_shutdown is not None:
        params['fast_shutdown'] = fast_shutdown
    return client.call('bdev_ftl_delete', params)


def bdev_ftl_unmap(client, name, lba, num_blocks):
    """Unmap range of LBAs.
    Args:
        name: Bdev name
        lba: Starting lba to be unmapped, aligned to 1024
        num_blocks: Number of blocks, aligned to 1024
    """
    params = dict()
    params['name'] = name
    params['lba'] = lba
    params['num_blocks'] = num_blocks
    return client.call('bdev_ftl_unmap', params)


def bdev_ftl_get_stats(client, name):
    """Get IO statistics for FTL bdev
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ftl_get_stats', params)


def bdev_ftl_get_properties(client, name):
    """Get FTL properties
    Args:
        name: Bdev name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_ftl_get_properties', params)


def bdev_ftl_set_property(client, name, ftl_property, value):
    """Set FTL property
    Args:
        name: Name of the bdev
        ftl_property: Name of the property to modify
        value: New value of the property to be set
    """
    params = dict()
    params['name'] = name
    params['ftl_property'] = ftl_property
    params['value'] = value
    return client.call('bdev_ftl_set_property', params)


def bdev_get_bdevs(client, name=None, timeout=None):
    """Get information about block devices (bdevs).
    Args:
        name: Bdev name to query (if omitted, query all bdevs)
        timeout: Time in ms to wait for the bdev with specified name to appear
    """
    params = dict()
    if name is not None:
        params['name'] = name
    if timeout is not None:
        params['timeout'] = timeout
    return client.call('bdev_get_bdevs', params)


def bdev_get_iostat(client, name=None, per_channel=None):
    """Get I/O statistics of block devices (bdevs).
    Args:
        name: Bdev name to query (if omitted, query all bdevs)
        per_channel: Display per channel IO stats for specified bdev
    """
    params = dict()
    if name is not None:
        params['name'] = name
    if per_channel is not None:
        params['per_channel'] = per_channel
    return client.call('bdev_get_iostat', params)


def bdev_reset_iostat(client, name=None, mode=None):
    """Reset I/O statistics of block devices (bdevs). Note that if one consumer resets I/O statistics, it affects all other consumers.
    Args:
        name: Bdev name to reset (if omitted, reset all bdevs)
        mode: Mode to reset I/O statistics: all, maxmin (default: all)
    """
    params = dict()
    if name is not None:
        params['name'] = name
    if mode is not None:
        params['mode'] = mode
    return client.call('bdev_reset_iostat', params)


def bdev_enable_histogram(client, name, enable):
    """Control whether collecting data for histogram is enabled for specified bdev.
    Args:
        name: Block device name
        enable: Enable or disable histogram on specified device
    """
    params = dict()
    params['name'] = name
    params['enable'] = enable
    return client.call('bdev_enable_histogram', params)


def bdev_get_histogram(client, name):
    """Get latency histogram for specified bdev.
    Args:
        name: Block device name
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_get_histogram', params)


def bdev_error_inject_error(client, name, io_type, error_type, num=None, queue_depth=None, corrupt_offset=None, corrupt_value=None):
    """Inject an error via an error bdev. Create an error bdev on base bdev first. Default 'num' value is 1 and if 'num' is set to zero,
    the specified injection is disabled.
    Args:
        name: Name of the error injection bdev
        io_type: IO type one of 'clear' 'read' 'write' 'unmap' 'flush' 'all'
        error_type: Error type one of 'failure' 'pending' 'corrupt_data' 'nomem'
        num: The number of commands you want to fail.(default:1)
        queue_depth: The queue depth at which to trigger the error
        corrupt_offset: The offset in bytes to xor with corrupt_value
        corrupt_value: The value for xor (1-255, 0 is invalid)
    """
    params = dict()
    params['name'] = name
    params['io_type'] = io_type
    params['error_type'] = error_type
    if num is not None:
        params['num'] = num
    if queue_depth is not None:
        params['queue_depth'] = queue_depth
    if corrupt_offset is not None:
        params['corrupt_offset'] = corrupt_offset
    if corrupt_value is not None:
        params['corrupt_value'] = corrupt_value
    return client.call('bdev_error_inject_error', params)


def bdev_set_qd_sampling_period(client, name, period):
    """Enable queue depth tracking on a specified bdev.
    Args:
        name: Block device name
        period: Period (in microseconds) at which to update the queue depth reading. If set to 0, polling will be disabled.
    """
    params = dict()
    params['name'] = name
    params['period'] = period
    return client.call('bdev_set_qd_sampling_period', params)


def bdev_set_qos_limit(client, name, rw_ios_per_sec=None, rw_mbytes_per_sec=None, r_mbytes_per_sec=None, w_mbytes_per_sec=None):
    """Set the quality of service rate limit on a bdev.
    Args:
        name: Block device name
        rw_ios_per_sec: Number of R/W I/Os per second to allow (>=1000, example: 20000). 0 means unlimited.
        rw_mbytes_per_sec: Number of R/W megabytes per second to allow (>=10, example: 100). 0 means unlimited.
        r_mbytes_per_sec: Number of Read megabytes per second to allow (>=10, example: 100). 0 means unlimited.
        w_mbytes_per_sec: Number of Write megabytes per second to allow (>=10, example: 100). 0 means unlimited.
    """
    params = dict()
    params['name'] = name
    if rw_ios_per_sec is not None:
        params['rw_ios_per_sec'] = rw_ios_per_sec
    if rw_mbytes_per_sec is not None:
        params['rw_mbytes_per_sec'] = rw_mbytes_per_sec
    if r_mbytes_per_sec is not None:
        params['r_mbytes_per_sec'] = r_mbytes_per_sec
    if w_mbytes_per_sec is not None:
        params['w_mbytes_per_sec'] = w_mbytes_per_sec
    return client.call('bdev_set_qos_limit', params)


def bdev_nvme_apply_firmware(client, bdev_name, filename):
    """Download and commit firmware to NVMe device.
    Args:
        bdev_name: Name of the NVMe block device
        filename: Filename of the firmware to download
    """
    params = dict()
    params['bdev_name'] = bdev_name
    params['filename'] = filename
    return client.call('bdev_nvme_apply_firmware', params)


def bdev_nvme_get_transport_statistics(client):
    """Get bdev_nvme poll group transport statistics."""
    params = dict()

    return client.call('bdev_nvme_get_transport_statistics', params)


def bdev_nvme_get_controller_health_info(client, name):
    """Display health log of the required NVMe bdev device.
    Args:
        name: Name of the NVMe bdev controller
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_nvme_get_controller_health_info', params)


def bdev_daos_create(client, num_blocks, block_size, pool, cont, name, oclass=None, uuid=None):
    """Construct DAOS bdev
    Args:
        num_blocks: Size of block device in blocks
        block_size: Block size in bytes; must be a power of 2 and at least 512
        pool: DAOS pool label or its uuid
        cont: DAOS cont label or its uuid
        name: Name of block device (also the name of the backend file on DAOS DFS)
        oclass: DAOS object class (default SX)
        uuid: UUID of new bdev
    """
    params = dict()
    params['num_blocks'] = num_blocks
    params['block_size'] = block_size
    params['pool'] = pool
    params['cont'] = cont
    params['name'] = name
    if oclass is not None:
        params['oclass'] = oclass
    if uuid is not None:
        params['uuid'] = uuid
    return client.call('bdev_daos_create', params)


def bdev_daos_delete(client, name):
    """Delete DAOS bdev
    Args:
        name: Name of DAOS bdev to delete
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_daos_delete', params)


def bdev_daos_resize(client, name, new_size):
    """Resize DAOS bdev.
    Args:
        name: Name of DAOS bdev to resize
        new_size: New bdev size of resize operation. The unit is MiB
    """
    params = dict()
    params['name'] = name
    params['new_size'] = new_size
    return client.call('bdev_daos_resize', params)


def bdev_nvme_start_mdns_discovery(client, name, svcname, hostnqn=None):
    """Starts an mDNS based discovery service for the specified service type for the auto-discovery of discovery controllers (NVMe
    TP-8009).
    Args:
        name: Prefix for NVMe discovery services found; 'n' + unique seqno + namespace ID will be appended to create unique names
        svcname: Service to discover: e.g. _nvme-disc._tcp
        hostnqn: NVMe-oF hostnqn to connect from
    """
    params = dict()
    params['name'] = name
    params['svcname'] = svcname
    if hostnqn is not None:
        params['hostnqn'] = hostnqn
    return client.call('bdev_nvme_start_mdns_discovery', params)


def bdev_nvme_stop_mdns_discovery(client, name):
    """Stops a mDNS discovery service. This includes detaching any controllers that were discovered via the service that is being
    stopped.
    Args:
        name: Name of mDNS discovery instance to stop
    """
    params = dict()
    params['name'] = name
    return client.call('bdev_nvme_stop_mdns_discovery', params)


def bdev_nvme_get_mdns_discovery_info(client):
    """Get the information about the mDNS discovery service instances."""
    params = dict()

    return client.call('bdev_nvme_get_mdns_discovery_info', params)
