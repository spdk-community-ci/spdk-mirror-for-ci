#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (C) 2023 Intel Corporation.  All rights reserved.

from setuptools import setup, find_packages
from spdk import __version__


setup(name='spdk', version=__version__, packages=find_packages())
