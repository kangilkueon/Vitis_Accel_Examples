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

#include <omp.h>

// XRT includes
#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

#define DATA_CORRUPTION_CHECK       (0)
#define KiB (1024)
#define MiB (1024 * 1024)
#define GiB (1024 * 1024 * 1024)
#define micro (1000 * 1000)

#define HOST_TO_SSD                 (1)
#define SSD_TO_HOST                 (1)
#define FPGA_TO_SSD                 (1)
#define SSD_TO_FPGA                 (1)
#define HOST_TO_FPGA                (1)
#define FPGA_TO_HOST                (1)

static const int THREAD_COUNT = 16;
static const int repeat = 5;
static const int buf_count = 18;
// static size_t max_size = 1 * GiB;//64 * MiB;//1 * GiB;//64 * MiB; // 64 MB
static size_t max_size = 512 * MiB;//1 * GiB;//64 * MiB; // 64 MB

static const std::string error_message =
    "Error: Result mismatch:\n"
    "i = %d CPU result = %d Device result = %d\n";

// This example illustrates the simple OpenCL example that performs
// buffer copy from one buffer to another
int main(int argc, char** argv) {
    omp_set_num_threads(THREAD_COUNT);

    // Command Line Parser
    sda::utils::CmdLineParser parser;

    // Switches
    //**************//"<Full Arg>",  "<Short Arg>", "<Description>", "<Default>"
    parser.addSwitch("--xclbin_file", "-x", "input binary file string", "");
    parser.addSwitch("--device_id", "-d", "device index", "0");
    parser.addSwitch("--input_file", "-f", "input file string", "/dev/nvme0n1");
    parser.parse(argc, argv);

    // Read settings
    auto binaryFile = parser.value("xclbin_file");
    std::string filename = parser.value("input_file");
    parser.parse(argc, argv);

    if (argc < 3) {
        parser.printHelp();
        return EXIT_FAILURE;
    }

    int buf_size[buf_count] = {4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288};
    char* input_data = (char*) aligned_alloc(4096, max_size);
    char* output_data = (char*) aligned_alloc(4096, max_size);

    for (size_t idx = 0; idx < max_size; idx++)
    {
        input_data[idx] = 'a' + idx % 27;
        if (idx % 27 == 0) input_data[idx] = '\n';
    }
    input_data[max_size - 1] = '\n';

    int nvmeFd = -1;
    std::cout << "############################################################\n";
    std::cout << "                  SSD Performance " << filename << "\n";
    std::cout << "############################################################\n";
    // Get access to the NVMe SSD.
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point end_time = std::chrono::high_resolution_clock::now();
    uint64_t process_time = 0;
#if (HOST_TO_SSD == 1)
    printf("Write Performance (Host to SSD)\n");
    for (int i = 0; i < buf_count; i++) {
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        start_time = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        #pragma omp for
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {
                int ret = pwrite(nvmeFd, (void*)(input_data + size * j), size, size * j);
                if (ret == -1) {
                    std::cout << "[Fail] SSD Write Performance Fails" << __LINE__ << " " << j << " " << ret << std::endl;
                    break;
                }
            }
            sync();
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size * repeat / process_time;
        double avg_latency = process_time / iter / repeat;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us\n", size, bandwidth, avg_latency, process_time);
        close(nvmeFd);
    }
#endif
    
#if (SSD_TO_HOST == 1)
    printf("Read Performance (SSD to Host)\n");
    for (int i = 0; i < buf_count; i++) {
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        start_time = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        #pragma omp for
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {
                int ret = pread(nvmeFd, (void*)(output_data + size * j), size, size * j);
                if (ret == -1) {
                    std::cout << "[Fail] SSD Read Performance Fails" << __LINE__ << " " << j << " " << ret << std::endl;
                    break;
                }
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size * repeat / process_time;
        double avg_latency = process_time / iter / repeat;
#if (DATA_CORRUPTION_CHECK == 1)
        for (size_t idx = 0; idx < max_size; idx++)
        {
            if (input_data[idx] != output_data[idx])
            {
                printf("Data corruption : %d\n", idx);
            }
        }
#endif
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us\n", size, bandwidth, avg_latency, process_time);
        close(nvmeFd);
    }
#endif

    // FPGA Init
    int device_index = stoi(parser.value("device_id"));
    auto device = xrt::device(device_index);
    auto device_name = device.get_info<xrt::info::device::name>();
    auto uuid = device.load_xclbin(binaryFile);
    std::cout << "############################################################\n";
    std::cout << "                  P2P Performance\n";
    std::cout << "############################################################\n";

    xrt::bo::flags flags = xrt::bo::flags::normal;
    auto krnl = xrt::kernel(device, uuid, "read_bandwidth");
    auto bo_in = xrt::bo(device, max_size, flags, krnl.group_id(0));
    auto bo_out = xrt::bo(device, max_size, flags, krnl.group_id(0));
    
#if (FPGA_TO_SSD == 1 || SSD_TO_FPGA == 1)
    flags = xrt::bo::flags::p2p;

    auto bo_p2p = xrt::bo(device, max_size, flags, krnl.group_id(0));
    auto p2p_host = bo_p2p.map<char*>();
    memcpy(p2p_host, input_data, max_size);
#endif

#if (FPGA_TO_SSD == 1)
    printf("Write Performance (FPGA to SSD)\n");
    for (int i = 0; i < buf_count; i++) {
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        start_time = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        #pragma omp for
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {
                // printf("%d",j);
                int ret = pwrite(nvmeFd, (void*)(p2p_host + size * j), size, size * j);
                if (ret == -1) {
                    std::cout << "[Fail] SSD Write Performance Fails " << __LINE__ << " " << i << " " << ret << std::endl;
                    std::cout << "[Fail] SSD Write Performance Fails " << __LINE__ << " " << j << " " << size << std::endl;
                    break;
                }
            }
            sync();
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size * repeat / process_time;
        double avg_latency = process_time / iter / repeat;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us\n", size, bandwidth, avg_latency, process_time);
        close(nvmeFd);
    }
#endif

#if (SSD_TO_FPGA == 1)
    printf("Read Performance (SSD to FPGA)\n");
    std::fill(p2p_host, p2p_host + max_size, 0);
    
    for (int i = 0; i < buf_count; i++) {
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        start_time = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        #pragma omp for
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {
                int ret = pread(nvmeFd, (void*)(p2p_host + size * j), size, size * j);
                if (ret == -1) {
                    std::cout << "[Fail] SSD Read Performance Fails" << __LINE__ << " " << j << " " << ret << std::endl;
                    break;
                }
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size * repeat / process_time;
        double avg_latency = process_time / iter / repeat;
#if (DATA_CORRUPTION_CHECK == 1)
        for (size_t idx = 0; idx < max_size; idx++)
        {
            if (input_data[idx] != p2p_bo1_map[idx])
            {
                printf("Data corruption[%dth] : %c\n", idx, p2p_bo1_map[idx]);
            }
        }
#endif
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us\n", size, bandwidth, avg_latency, process_time);
        close(nvmeFd);
    }
#endif
    std::cout << "############################################################\n";
    std::cout << "                  FPGA Performance " << uuid << "\n";
    std::cout << "############################################################\n";

#if (HOST_TO_FPGA == 1)
    printf("Write Performance (Host to FPGA (by bo::write))\n");
    for (int i = 0; i < buf_count; i++) {
        auto krnl = xrt::kernel(device, uuid, "bandwidth");
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        // auto bo_a = xrt::bo(device, size, krnl.group_id(0));
        start_time = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        #pragma omp for
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {
                int offset = j * size;

                bo_in.write((void*)input_data, size, offset);
                bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE, size, offset);   
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size * repeat / process_time;
        double avg_latency = process_time / iter / repeat;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us\n", size, bandwidth, avg_latency, process_time);
    }
#endif

#if (FPGA_TO_HOST == 1)
#define Simple_CHECK (0)
    printf("Read Performance (FPGA to Host (by bo::read))\n");
    for (int i = 0; i < buf_count; i++) {
        auto krnl = xrt::kernel(device, uuid, "bandwidth");
        int64_t size = buf_size[i] * KiB;
        int64_t iter = max_size / size;
        
        std::fill(output_data, output_data + size, 0);
        auto run = krnl(bo_in, bo_out, max_size / sizeof(int));
        run.wait();

        start_time = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        #pragma omp for
        for (int _repeat = 0; _repeat < repeat; _repeat++) {
            for (int j = 0; j < iter; j++) {
                int offset = j * size;
#if (Simple_CHECK || DATA_CORRUPTION_CHECK == 1)
                bo_out.write(input_data, size, offset);
                bo_out.sync(XCL_BO_SYNC_BO_TO_DEVICE, size, offset);
#endif
                // auto run = krnl(bo_host, bo_device, size);
                // auto run = krnl(bo_test, bo_test, size);
                // run.wait();
#if 0
                auto input_buffer_mapped = bo_out.map<int*>();
                bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE, size, offset);
#else
                bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE, size, offset);
                // bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
                bo_out.read((void*) output_data, size, offset);
#endif
#if (Simple_CHECK || DATA_CORRUPTION_CHECK == 1)
                for (size_t idx = 0; idx < size / sizeof(char *); idx++)
                {
                    if (input_data[idx] != output_data[idx])
                    // if (idx < 100)
                    {
                        printf("Data corruption[%dth] : %c vs %c\n", idx, input_data[idx], output_data[idx]);
                    }
                }
#endif
            }
                //bo_b.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
                //printf("hello3?\n");
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = size * iter * repeat / process_time;
        double avg_latency = process_time / iter / repeat;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lu us (iter:%u)\n", size, bandwidth, avg_latency, process_time, iter);
    }
#endif

    free(input_data);
    free(output_data);
    return 0;
}
