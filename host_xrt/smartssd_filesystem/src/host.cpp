/**
* Copyright (C) 2019-2021 Xilinx, Inc
*
* Licensed under the Apache License, Version 2.0 (the "License"). You may
* not use this file except in compliance with the License. A copy of the
* License is located at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
* License for the specific language governing permissions and limitations
* under the License.
*/
#include "cmdlineparser.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iosfwd>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <stdio.h>
#include <fcntl.h>

// XRT includes
#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

#define DATA_CORRUPTION_CHECK       (0)
#define KiB (1024)
#define MiB (1024 * 1024)
#define GiB (1024 * 1024 * 1024)
#define micro (1000 * 1000)
static const int repeat = 1;
static const int buf_count = 10;
static size_t max_size = 64 * MiB; //8 * MiB; // 64 MB

static const std::string error_message =
    "Error: Result mismatch:\n"
    "i = %d CPU result = %d Device result = %d\n";

// This example illustrates the simple OpenCL example that performs
// buffer copy from one buffer to another
int main(int argc, char** argv) {
    // Command Line Parser
    sda::utils::CmdLineParser parser;

    // Switches
    //**************//"<Full Arg>",  "<Short Arg>", "<Description>", "<Default>"
    parser.addSwitch("--xclbin_file", "-x", "input binary file string", "");
    parser.addSwitch("--device_id", "-d", "device index", "0");
    parser.addSwitch("--input_file", "-f", "input file string", "/mnt/smartssd/test.txt");
    parser.parse(argc, argv);

    // Read settings
    auto binaryFile = parser.value("xclbin_file");
    std::string filename = parser.value("input_file");
    parser.parse(argc, argv);

    if (argc < 3) {
        parser.printHelp();
        return EXIT_FAILURE;
    }

    int buf_size[buf_count] = {512, 4 * KiB, 8 * KiB, 16 * KiB, 32 * KiB, 64 * KiB, 128 * KiB, 256 * KiB, 512 * KiB, 1024 * KiB};
    char* input_data = (char*) aligned_alloc(4096, max_size);
    char* output_data = (char*) aligned_alloc(4096, max_size);

    for (size_t idx = 0; idx < max_size; idx++)
    {
        input_data[idx] = idx && 0xFF;
    }

    int nvmeFd = -1;
    uint64_t process_time = 0;

    // FPGA Init
    int device_index = stoi(parser.value("device_id"));
    auto device = xrt::device(device_index);
    auto device_name = device.get_info<xrt::info::device::name>();
    auto uuid = device.load_xclbin(binaryFile);

    // Precon - Write File
    std::cout << "############################################################\n";
    std::cout << "                  Precondition : " << filename << "\n";
    std::cout << "############################################################\n";
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    nvmeFd = open(filename.c_str(), O_CREAT | O_RDWR | O_DIRECT);
    if (nvmeFd <= 0) {
        std::cout << "ERROR" << nvmeFd << std::endl;
        exit(-1);
    }
    int ret = pwrite(nvmeFd, (void*) input_data, max_size, 0);
    if (ret == -1) {
        std::cout << "[Fail] SSD Write Performance Fails" << __LINE__ << " " << ret << std::endl;
        return EXIT_FAILURE;
    }
    sync();
    close(nvmeFd);
    std::chrono::high_resolution_clock::time_point end_time = std::chrono::high_resolution_clock::now();
    free(input_data);

    process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    int64_t bandwidth = max_size / process_time;
    double avg_latency = process_time;
    printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us\n", max_size, bandwidth, avg_latency, process_time);

    // step 1 Open file -> Read file -> Compute -> Get Data
    std::cout << "############################################################\n";
    std::cout << "                  Test start " << filename << "\n";
    std::cout << "############################################################\n";
    xrt::bo::flags flags = xrt::bo::flags::p2p;

    auto krnl = xrt::kernel(device, uuid, "bandwidth");

    std::chrono::high_resolution_clock::time_point file_open_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point pread_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point kernel_end_time = std::chrono::high_resolution_clock::now();
    // Get access to the NVMe SSD.

    // Open file
    for (int i = 0; i < buf_count; i++) {
        uint64_t file_open_time = 0;
        uint64_t pread_time = 0;
        uint64_t kernel_time = 0;
        
        start_time = std::chrono::high_resolution_clock::now();
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        file_open_end_time = std::chrono::high_resolution_clock::now();
        int size = buf_size[i];
        int iter = max_size / size;

        file_open_time = std::chrono::duration_cast<std::chrono::microseconds>(file_open_end_time - start_time).count();
        auto p2p_bo_in = xrt::bo(device, size, flags, krnl.group_id(0));
        auto p2p_bo_in_map = p2p_bo_in.map<char*>();
        auto p2p_bo_out = xrt::bo(device, size, flags, krnl.group_id(0));
        auto p2p_bo_out_map = p2p_bo_out.map<char*>();
        std::fill(p2p_bo_in_map, p2p_bo_in_map + size, 0);
        std::fill(p2p_bo_out_map, p2p_bo_out_map + size, 0);
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {

                start_time = std::chrono::high_resolution_clock::now();
                int ret = pread(nvmeFd, (void*)p2p_bo_in_map, size, 0);
                if (ret == -1) {
                    std::cout << "[Fail] SSD Read Performance Fails" << __LINE__ << " " << j << " " << ret << std::endl;
                    return EXIT_FAILURE;
                }
                pread_end_time = std::chrono::high_resolution_clock::now();

                auto run = krnl(p2p_bo_in, p2p_bo_out, size);
                run.wait();
                kernel_end_time = std::chrono::high_resolution_clock::now();
                if (std::memcmp(p2p_bo_in_map, p2p_bo_out_map, size))
                    printf("Data corruption\n");
                pread_time = pread_time + std::chrono::duration_cast<std::chrono::nanoseconds>(pread_end_time - start_time).count();
                kernel_time = kernel_time + std::chrono::duration_cast<std::chrono::microseconds>(kernel_end_time - pread_end_time).count();
            }
        }
        
        printf("IO_Size : %d B\tFile open : %ld us\tPREAD : %lu us\tKernel:%lu us\n", size, file_open_time, pread_time / iter / 1000, kernel_time / iter);
        close(nvmeFd);
    }
    return 0;
}
