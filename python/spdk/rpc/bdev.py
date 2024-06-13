#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2017 Intel Corporation.
#  All rights reserved.

import sys

from .common_warning import warning_decorator, geneated_functions


for func in geneated_functions:
    setattr(sys.modules[__name__], func.__name__, warning_decorator(__name__)(func))
