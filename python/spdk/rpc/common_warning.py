#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2024 Intel Corporation.
#  All rights reserved.

import spdk.rpc.rpc as rpc


def warning_decorator(module_name):
    def decorator(func):
        def wrapper(*args, **kwargs):
            print(
                f"WARNING: import using {module_name}.{func.__name__} is deprecated",
                f"please use spdk.rpc.{func.__name__} instead.",
            )
            result = func(*args, **kwargs)
            return result
        return wrapper
    return decorator


geneated_functions = [
    getattr(rpc, name) for name in dir(rpc) if callable(getattr(rpc, name))
]
