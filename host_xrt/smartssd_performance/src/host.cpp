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

#define KiB (1024)
#define MiB (1024 * 1024)
#define GiB (1024 * 1024 * 1024)
#define micro (1000 * 1000)
static const int DATA_SIZE = 1024;
static const int repeat = 1;
static const int buf_count = 9;
static const size_t max_size = 1 * MiB; // 1GB

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

    int buf_size[buf_count] = {4, 8, 16, 32, 64, 128, 256, 512, 1024};
    char* input_data = (char*) aligned_alloc(4096, max_size);
    char* output_data = (char*) aligned_alloc(4096, max_size);

    for (size_t idx = 0; idx < max_size;)
    {
        input_data[idx++] = 0xd;
        input_data[idx++] = 0xe;
        input_data[idx++] = 0xa;
        input_data[idx++] = 0xd;
        input_data[idx++] = 0xb;
        input_data[idx++] = 0xe;
        input_data[idx++] = 0xe;
        input_data[idx++] = 0xf;
    }
    int nvmeFd = -1;
    std::cout << "############################################################\n";
    std::cout << "                  SSD Performance " << filename << "\n";
    std::cout << "############################################################\n";
    // Get access to the NVMe SSD.
    

    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point end_time = std::chrono::high_resolution_clock::now();
    int64_t process_time = 0;
    printf("Write Performance (Host to SSD)\n");
    for (int i = 0; i < buf_count; i++) {
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        start_time = std::chrono::high_resolution_clock::now();
        for (int _repeat = 0; _repeat < repeat; _repeat++)
            for (int j = 0; j < iter; j++) {
                int ret = pwrite(nvmeFd, (void*)(input_data + size * j), size, size * j);
                if (ret == -1) {
                    std::cout << "[Fail] SSD Write Performance Fails" << __LINE__ << " " << j << " " << ret << std::endl;
                    return EXIT_FAILURE;
                }
            }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size / process_time * repeat;
        double avg_latency = process_time / iter;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lld us\n", size, bandwidth, avg_latency, process_time);
        close(nvmeFd);
    }
    
    printf("Read Performance (SSD to Host)\n");
    for (int i = 0; i < buf_count; i++) {
        nvmeFd = open(filename.c_str(), O_RDWR | O_DIRECT);
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        start_time = std::chrono::high_resolution_clock::now();
        for (int _repeat = 0; _repeat < repeat; _repeat++)
        for (int j = 0; j < iter; j++) {
            int ret = pread(nvmeFd, (void*)(output_data + size * j), size, size * j);
            if (ret == -1) {
                std::cout << "[Fail] SSD Read Performance Fails" << __LINE__ << " " << j << " " << ret << std::endl;
                return EXIT_FAILURE;
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size / process_time * repeat;
        double avg_latency = process_time / iter;
        for (size_t idx = 0; idx < max_size; idx++)
        {
            if (input_data[idx] != output_data[idx])
            {
                printf("Data corruption : %d\n", idx);
            }
        }
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lld us\n", size, bandwidth, avg_latency, process_time);
        close(nvmeFd);
    }
    // FPGA Init
    int device_index = stoi(parser.value("device_id"));
    auto device = xrt::device(device_index);
    auto device_name = device.get_info<xrt::info::device::name>();
    auto uuid = device.load_xclbin(binaryFile);
    auto krnl = xrt::kernel(device, uuid, "bandwidth");
    std::cout << "############################################################\n";
    std::cout << "                  FPGA Performance " << uuid << "\n";
    std::cout << "############################################################\n";

#if 0
    printf("Write Performance (Host to FPGA)\n");
    for (int i = 0; i < buf_count; i++) {
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        auto bo_a = xrt::bo(device, size, krnl.group_id(0));
        auto bo_b = xrt::bo(device, size, krnl.group_id(1));
        auto bo_a_map = bo_a.map<char*>();
        auto bo_b_map = bo_b.map<char*>();
        std::fill(bo_a_map, bo_a_map + size, 0);
        std::fill(bo_b_map, bo_b_map + size, 0);
            for (size_t idx = 0; idx < size;)
        {
            bo_a_map[idx++] = 0xd;
            bo_a_map[idx++] = 0xe;
            bo_a_map[idx++] = 0xa;
            bo_a_map[idx++] = 0xd;
            bo_a_map[idx++] = 0xb;
            bo_a_map[idx++] = 0xe;
            bo_a_map[idx++] = 0xe;
            bo_a_map[idx++] = 0xf;
        }
        start_time = std::chrono::high_resolution_clock::now();
        for (int _repeat = 0; _repeat < repeat; _repeat++)
            for (int j = 0; j < iter; j++) {

                bo_a.sync(XCL_BO_SYNC_BO_TO_DEVICE);
                auto run = krnl(bo_a_map, bo_b_map, size);
                run.wait();
                bo_b.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        int z=0;
        printf("%x%x%x%x%x%x%x%x%x\n", bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++]);
            }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size / process_time * repeat;
        double avg_latency = process_time / iter;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lld us\n", size, bandwidth, avg_latency, process_time);
        //free(bo_a_map);
        //free((void*) bo_a);
    }

    printf("Read Performance (FPGA to Host)\n");
    for (int i = 0; i < buf_count; i++) {
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        auto bo_b = xrt::bo(device, size, krnl.group_id(0));
        auto bo_b_map = bo_b.map<char*>();
        std::fill(bo_b_map, bo_b_map + size, 0);
        start_time = std::chrono::high_resolution_clock::now();
        for (int _repeat = 0; _repeat < repeat; _repeat++)
            for (int j = 0; j < iter; j++) {
                /* auto bo_a_map = bo_a.map<int*>();
                for (size_t idx = 0; idx < size;)
                {
                    bo_a_map[idx++] = 0xd;
                    bo_a_map[idx++] = 0xe;
                    bo_a_map[idx++] = 0xa;
                    bo_a_map[idx++] = 0xd;
                    bo_a_map[idx++] = 0xb;
                    bo_a_map[idx++] = 0xe;
                    bo_a_map[idx++] = 0xe;
                    bo_a_map[idx++] = 0xf;
                }
                */
    printf("Read Performance (FPGA to Host)\n");
                auto run = krnl(bo_b_map, bo_b_map, size);
                run.wait();
                bo_b.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
            }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        
        int z=0;
        printf("%x%x%x%x%x%x%x%x%x\n", bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++], bo_b_map[z++]);
        int64_t bandwidth = max_size / process_time * repeat;
        double avg_latency = process_time / iter;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lld us\n", size, bandwidth, avg_latency, process_time);
        //free(bo_a_map);
        //free((void*) bo_a);
    }
    
#endif
    auto bo_a = xrt::bo(device, max_size, krnl.group_id(0));
    auto bo_a_map = bo_a.map<char*>();
    std::fill(bo_a_map, bo_a_map + max_size, 0);

    for (size_t idx = 0; idx < max_size;)
    {
        bo_a_map[idx++] = 0xd;
        bo_a_map[idx++] = 0xe;
        bo_a_map[idx++] = 0xa;
        bo_a_map[idx++] = 0xd;
        bo_a_map[idx++] = 0xb;
        bo_a_map[idx++] = 0xe;
        bo_a_map[idx++] = 0xe;
        bo_a_map[idx++] = 0xf;
    }

    printf("Write Performance (Host to FPGA)\n");
    for (int i = 0; i < buf_count; i++) {
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        xrt::bo::flags flags = xrt::bo::flags::host_only;
        auto bo_b = xrt::bo(device, size, krnl.group_id(0));
        auto bo_b_map = bo_b.map<char*>();
        std::fill(bo_b_map, bo_b_map + size, 0);

        char* test = (char*)aligned_alloc(4096, size);
        start_time = std::chrono::high_resolution_clock::now();
        for (int _repeat = 0; _repeat < repeat; _repeat++)
            for (int j = 0; j < iter; j++) {
                bo_a.write((void*)(input_data + size * j), size, size * j);
                auto run = xrt::run(krnl);//l(bo_a_map, bo_b_map, size);
                run.set_arg(0, test);
                run.set_arg(1, bo_a_map);
                run.set_arg(2, size / 4);
                run.start();
                run.wait();
        int z=0;
        printf("%x%x%x%x%x%x%x%x%x\n", test[z++], test[z++], test[z++], test[z++], test[z++], test[z++], test[z++], test[z++], test[z++], test[z++], test[z++]);
            }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size / process_time * repeat;
        double avg_latency = process_time / iter;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lld us\n", size, bandwidth, avg_latency, process_time);
        //free(bo_a_map);
        //free((void*) bo_a);
    }

    printf("Read Performance (FPGA to Host)\n");
    for (int i = 0; i < buf_count; i++) {
        int size = buf_size[i] * KiB;
        int iter = max_size / size;
        
        start_time = std::chrono::high_resolution_clock::now();
        for (int _repeat = 0; _repeat < repeat; _repeat++)
            for (int j = 0; j < iter; j++) {
                char *test = (char*) aligned_alloc(4096, size);
                bo_a.read((void*)test, size, size * j);
            }
        end_time = std::chrono::high_resolution_clock::now();
        process_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        int64_t bandwidth = max_size / process_time * repeat;
        double avg_latency = process_time / iter;
        printf("IO_Size : %d B\tBandwidth : %ld MB/s\tavg_latench : %f us\ttotal_time:%lld us\n", size, bandwidth, avg_latency, process_time);
    }


    free(input_data);
    return 0;
}
