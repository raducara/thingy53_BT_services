#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thingy53_BT_services)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/bme68x_service.c
  src/adxl362_service.c
  src/bmm150_service.c
  src/BH1749NUC_colorsens_service.c
  src/debug_service.c
)

# NORDIC SDK APP END
