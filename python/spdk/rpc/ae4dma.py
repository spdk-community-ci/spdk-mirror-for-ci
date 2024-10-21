#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Advanced Micro Devices, Inc
#  All rights reserved.

from spdk.rpc.helpers import deprecated_alias


def ae4dma_scan_accel_module(client):
    """Enable AE4DMA accel module.
    """
    return client.call('ae4dma_scan_accel_module')
