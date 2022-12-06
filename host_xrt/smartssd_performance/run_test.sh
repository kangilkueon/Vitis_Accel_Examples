#!/bin/bash

source /opt/xilinx/xrt/setup.sh
make host
./smartssd_performance -x bandwidth.xclbin