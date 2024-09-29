/*
 * Copyright (c) 2021-2022 Baidu.com, Inc. All Rights Reserved.
 * Copyright (c) 2022-2023 Jianzhang Peng. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Jianzhang Peng (pengjianzhang@baidu.com)
 *         Jianzhang Peng (pengjianzhang@gmail.com)
 */

#pragma once
#include "common.h"
#include <iostream>
#include <iomanip>

namespace nicc {
struct net_stats {
    /* App level */
    uint64_t app_tx_msg_num = 0;
    uint64_t app_rx_msg_num = 0;

    uint64_t app_tx_invoke_times = 0;
    uint64_t app_tx_avg_duration = 0;
    uint64_t app_tx_max_duration = 0;
    uint64_t app_tx_min_duration = std::numeric_limits<uint64_t>::max();
    uint64_t app_tx_stall_avg_duration = 0;
    uint64_t app_tx_stall_max_duration = 0;
    uint64_t app_tx_stall_min_duration = std::numeric_limits<uint64_t>::max();
    // uint64_t app_tx_mbuf_min_addr = std::numeric_limits<uint64_t>::max();
    // uint64_t app_tx_mbuf_max_addr = 0;

    void* app_tx_mbuf_trace_addr = nullptr;
    uint64_t app_tx_mbuf_reuse_tmp_interval = 0;
    uint64_t app_tx_mbuf_reuse_interval = 0;
    uint64_t app_tx_nb_traced_mbuf = 0;
    
    uint64_t app_rx_invoke_times = 0;
    uint64_t app_rx_avg_duration = 0;
    uint64_t app_rx_max_duration = 0;
    uint64_t app_rx_min_duration = std::numeric_limits<uint64_t>::max();
    uint64_t app_rx_stall_avg_duration = 0;
    uint64_t app_rx_stall_max_duration = 0;
    uint64_t app_rx_stall_min_duration = 0;

    /* Dispatcher level */
    uint64_t disp_tx_pkt_num = 0;
    uint64_t disp_rx_pkt_num = 0;
    uint64_t disp_tx_duration = 0;
    uint64_t disp_tx_stall_duration = 0;
    uint64_t disp_rx_duration = 0;
    uint64_t disp_rx_stall_duration = 0;

    /* NIC level */
    uint64_t nic_tx_pkt_num = 0;
    uint64_t nic_rx_pkt_num = 0;
    uint64_t nic_tx_duration = 0;
    uint64_t nic_rx_duration = 0;
    double nic_rx_cpt = 0;
    uint64_t nic_rx_times = 0;

    /* Diagnose */
    uint64_t app_apply_mbuf_stalls = 0;
    uint64_t app_enqueue_drops = 0;
    uint32_t mbuf_alloc_times = 0;
    uint64_t mbuf_usage = 0;
    uint64_t disp_enqueue_drops = 0;
};

struct perf_stats {
    double e2e_throughput_ = 0; // Mpps
    double e2e_compl_ = 0;    // us
    /// Breakdown
    double app_tx_throughput_ = 0;
    double app_rx_throughput_ = 0;

    double app_tx_compl_ = 0;
    double app_tx_compl_max_ = 0;
    double app_tx_compl_min_ = std::numeric_limits<uint64_t>::max();
    double app_tx_compl_avg_ = 0;
    double app_tx_stall_ = 0;
    double app_tx_stall_avg_ = 0;
    double app_tx_stall_min_ = 0;
    double app_tx_stall_max_ = 0;
 
    double app_rx_compl_ = 0;
    double app_rx_compl_max_ = 0;
    double app_rx_compl_min_ = std::numeric_limits<uint64_t>::max();
    double app_rx_compl_avg_ = 0;
    double app_rx_stall_ = 0;
    double app_rx_stall_avg_ = 0;
    double app_rx_stall_min_ = 0;
    double app_rx_stall_max_ = 0;

    double disp_tx_throughput_ = 0;
    double disp_rx_throughput_ = 0;
    double disp_tx_compl_ = 0;
    double disp_tx_stall_ = 0;
    double disp_rx_compl_ = 0;
    double disp_rx_stall_ = 0;

    double disp_mbuf_usage = 0;

    double nic_tx_throughput_ = 0;
    double nic_rx_throughput_ = 0;
    double nic_tx_compl_ = 0;
    double nic_rx_compl_ = 0;

    

    public:
        void print_perf_stats(uint8_t duration) {
            /// check min value
            app_tx_stall_min_ = app_tx_stall_min_ > app_tx_stall_max_ ? 9999 : app_tx_stall_min_;
            app_rx_stall_min_ = app_rx_stall_min_ > app_rx_stall_max_ ? 9999 : app_rx_stall_min_;
            app_tx_compl_min_ = app_tx_compl_min_ > app_tx_compl_max_ ? 9999 : app_tx_compl_min_;
            app_rx_compl_min_ = app_rx_compl_min_ > app_rx_compl_max_ ? 9999 : app_rx_compl_min_;
            
            std::cout   << std::fixed;
            std::cout   << std::setprecision(3);
            std::cout   << "---------------------------------------------------------------------"
                        << "---------------------------------------------------------------------"
                        << "---------------------------------------------------------------------"
                        << std::endl;
            std::cout   << std::left 
                        << std::setw(20) << "DPerf Statistics" 
                        << std::setw(20) << "Thpl. (Mpps)" 
                        << std::setw(20) << "Avg. [/P]"          // per packet
                        << std::setw(20) << "Avg. Stall [/P]"    // per packet
                        << std::setw(20) << "Max Stall. [/B]"    // per batch
                        << std::setw(20) << "Min Stall. [/B]"    // per batch
                        << std::setw(20) << "Avg Stall. [/B]"    // per batch
                        << std::setw(20) << "Max Coml. [/B]"     // per batch
                        << std::setw(20) << "Min Coml. [/B]"     // per batch
                        << std::setw(20) << "Avg Coml. [/B]"     // per batch
                        << std::endl;
            std::cout   << "---------------------------------------------------------------------" 
                        << "---------------------------------------------------------------------"
                        << "---------------------------------------------------------------------"
                        << std::endl;
            /// End-to-end
            std::cout << std::left 
                        << std::setw(20) << "End-to-end" 
                        << std::setw(20) << e2e_throughput_ 
                        << std::setw(20) << e2e_compl_ 
                        << std::endl;
            /// App
            std::cout << std::left 
                        << std::setw(20) << "app_tx" 
                        << std::setw(20) << app_tx_throughput_ 
                        << std::setw(20) << app_tx_compl_ + app_tx_stall_
                        << std::setw(20) << app_tx_stall_
                        << std::setw(20) << app_tx_stall_max_
                        << std::setw(20) << app_tx_stall_min_
                        // zhuobin: we also record the mbuf usage here to identify whether app stalls: (1) conflict; (2) required mbuf is too many
                        << std::setw(20) << std::to_string(app_tx_stall_avg_) + "(" + std::to_string(disp_mbuf_usage) + ")"
                        << std::setw(20) << app_tx_compl_max_
                        << std::setw(20) << app_tx_compl_min_
                        << std::setw(20) << app_tx_compl_avg_
                        << std::endl;
            std::cout << std::left 
                        << std::setw(20) << "app_rx" 
                        << std::setw(20) << app_rx_throughput_ 
                        << std::setw(20) << app_rx_compl_ + app_rx_stall_ 
                        << std::setw(20) << app_rx_stall_
                        << std::setw(20) << app_rx_stall_max_
                        << std::setw(20) << app_rx_stall_min_
                        << std::setw(20) << app_rx_stall_avg_
                        << std::setw(20) << app_rx_compl_max_
                        << std::setw(20) << app_rx_compl_min_
                        << std::setw(20) << app_rx_compl_avg_
                        << std::endl;
            /// Dispatcher
            std::cout << std::left 
                        << std::setw(20) << "disp_tx" 
                        << std::setw(20) << disp_tx_throughput_ 
                        << std::setw(20) << disp_tx_compl_ + disp_tx_stall_
                        << std::setw(20) << disp_tx_stall_
                        << std::endl;
            std::cout << std::left
                        << std::setw(20) << "disp_rx" 
                        << std::setw(20) << disp_rx_throughput_ 
                        << std::setw(20) << disp_rx_compl_ + disp_rx_stall_ 
                        << std::setw(20) << disp_rx_stall_
                        << std::endl;
            /// NIC
            std::cout << std::left 
                        << std::setw(20) << "nic_tx"
                        << std::setw(20) << nic_tx_throughput_
                        << std::setw(15) << nic_tx_compl_
                        << std::endl;
            std::cout << std::left
                        << std::setw(20) << "nic_rx"
                        << std::setw(20) << nic_rx_throughput_
                        << std::setw(15) << nic_rx_compl_
                        << std::endl;
            std::cout   << "---------------------------------------------------------------------"
                        << "---------------------------------------------------------------------"
                        << "---------------------------------------------------------------------"
                        << std::endl;
            std::cout   << std::endl;
        }
};

#define net_stats_app_tx(n)      do {stats_->app_tx_msg_num += (n);} while (0)
#define net_stats_app_rx(n)     do {stats_->app_rx_msg_num += (n);} while (0)

#if PERF_TEST_LAT == 1 && PERF_TEST_LAT_MIN_MAX == 1
#define net_stats_app_tx_duration(n) do {                                       \
    uint64_t duration_tick = rdtsc() - n;                                       \
    stats_->app_tx_invoke_times += 1;                                           \
    stats_->app_tx_avg_duration += duration_tick;                               \
    stats_->app_tx_min_duration = stats_->app_tx_min_duration > duration_tick   \
                                ? duration_tick : stats_->app_tx_min_duration;  \
    stats_->app_tx_max_duration = stats_->app_tx_max_duration < duration_tick   \
                                ? duration_tick : stats_->app_tx_max_duration;  \
} while (0)
#define net_stats_app_tx_stall_duration(n) do {                                             \
    uint64_t duration_tick = rdtsc() - n;                                                   \
    stats_->app_tx_stall_avg_duration += duration_tick;                                     \
    stats_->app_tx_stall_max_duration = stats_->app_tx_stall_max_duration < duration_tick   \
                                    ? duration_tick : stats_->app_tx_stall_max_duration;    \
    stats_->app_tx_stall_min_duration = stats_->app_tx_stall_min_duration > duration_tick   \
                                    ? duration_tick : stats_->app_tx_stall_min_duration;    \
} while (0)

#define net_stats_app_rx_duration(n) do {                                       \
    uint64_t duration_tick = rdtsc() - n;                                       \
    stats_->app_rx_invoke_times += 1;                                           \
    stats_->app_rx_avg_duration += duration_tick;                               \
    stats_->app_rx_min_duration = stats_->app_rx_min_duration > duration_tick   \
                                ? duration_tick : stats_->app_rx_min_duration;  \
    stats_->app_rx_max_duration = stats_->app_rx_max_duration < duration_tick   \
                                ? duration_tick : stats_->app_rx_max_duration;  \
} while (0)
#define net_stats_app_rx_stall_duration(n) do {                                             \
    uint64_t duration_tick = rdtsc() - n;                                                   \
    stats_->app_rx_stall_avg_duration += duration_tick;                                     \
    stats_->app_rx_stall_max_duration = stats_->app_rx_stall_max_duration < duration_tick   \
                                    ? duration_tick : stats_->app_rx_stall_max_duration;    \
    stats_->app_rx_stall_min_duration = stats_->app_rx_stall_min_duration > duration_tick   \
                                    ? duration_tick : stats_->app_rx_stall_min_duration;    \
} while (0)
#elif PERF_TEST_LAT == 1 && PERF_TEST_LAT_MIN_MAX == 0
#define net_stats_app_tx_duration(n) do {                                       \
    stats_->app_tx_avg_duration += rdtsc() - n;                                \
} while (0)
#define net_stats_app_tx_stall_duration(n) do {                                 \
    stats_->app_tx_stall_avg_duration += rdtsc() - n;                          \
} while (0)
#define net_stats_app_rx_duration(n) do {                                       \
    stats_->app_rx_avg_duration += rdtsc() - n;                                \
} while (0)
#define net_stats_app_rx_stall_duration(n) do {                                 \
    stats_->app_rx_stall_avg_duration += rdtsc() - n;                          \
} while (0)
#endif

#if PERT_TEST_MBUF_RANGE == 1
#define net_stats_app_tx_mbuf_reuse_interval(mbuf_addr) do {                                \
    if(unlikely(stats_->app_tx_mbuf_trace_addr == nullptr)){                                \
        stats_->app_tx_mbuf_trace_addr = mbuf_addr;                                         \
        stats_->app_tx_nb_traced_mbuf += 1;                                                 \
    } else {                                                                                \
        if(unlikely(mbuf_addr == stats_->app_tx_mbuf_trace_addr)){                          \
            stats_->app_tx_mbuf_reuse_interval += stats_->app_tx_mbuf_reuse_tmp_interval;   \
            stats_->app_tx_mbuf_trace_addr = nullptr;                                       \
            stats_->app_tx_mbuf_reuse_tmp_interval = 0;                                     \
        } else {                                                                            \
            stats_->app_tx_mbuf_reuse_tmp_interval += 1;                                    \
        }                                                                                   \
    }                                                                                       \
} while (0)                                                 
#else
#define net_stats_app_tx_mbuf_reuse_interval(mbuf_addr) do { /* do nothing */ } while (0)
#endif

#define net_stats_disp_tx(n)    do {stats_->disp_tx_pkt_num += (n);} while (0)
#define net_stats_disp_rx(n)    do {stats_->disp_rx_pkt_num += (n);} while (0)
#define net_stats_disp_tx_duration(n) do {stats_->disp_tx_duration += rdtsc() - (n);} while (0)
#define net_stats_disp_tx_stall_duration(n) do {stats_->disp_tx_stall_duration += rdtsc() - (n);} while (0)
#define net_stats_disp_rx_duration(n) do {stats_->disp_rx_duration += rdtsc() - (n);} while (0)
#define net_stats_disp_rx_stall_duration(n) do {stats_->disp_rx_stall_duration += rdtsc() - (n);} while (0)

#define net_stats_nic_tx(n)     do {stats_->nic_tx_pkt_num += (n);} while (0)
#define net_stats_nic_rx(m, n)     do {stats_->nic_rx_pkt_num += (m) - (n);} while (0)
#define net_stats_nic_tx_duration() do {stats_->nic_tx_duration += rdtsc() - stats_->nic_tx_start_tick;} while (0)
#define net_stats_nic_rx_duration(m, n) do {stats_->nic_rx_duration += (m) - (n);} while (0)
#define net_stats_nic_rx_cpt(n) do {stats_->nic_rx_cpt += (n); stats_->nic_rx_times++;} while (0)

/* Diagnose */
#define net_stats_app_apply_mbuf_stalls() do {stats_->app_apply_mbuf_stalls++;} while (0)
#define net_stats_app_drops(n)    do {stats_->app_enqueue_drops += n;} while (0)
#define net_stats_mbuf_usage(n) do{stats_->mbuf_alloc_times++; stats_->mbuf_usage += n;} while(0)
#define net_stats_disp_enqueue_drops(n) do {stats_->disp_enqueue_drops += n;} while (0)

static inline void net_stats_init(struct net_stats *stats) {
    memset(stats, 0, sizeof(struct net_stats));
    stats->app_tx_min_duration = std::numeric_limits<uint64_t>::max();
    stats->app_rx_min_duration = std::numeric_limits<uint64_t>::max();
    stats->app_tx_stall_min_duration = std::numeric_limits<uint64_t>::max();
    stats->app_rx_stall_min_duration = std::numeric_limits<uint64_t>::max();
}
static inline void perf_stats_init(struct perf_stats *stats) {
    memset(stats, 0, sizeof(struct perf_stats));
    stats->app_tx_compl_min_ = std::numeric_limits<uint64_t>::max();
    stats->app_rx_compl_min_ = std::numeric_limits<uint64_t>::max();
    stats->app_tx_stall_min_ = std::numeric_limits<uint64_t>::max();
    stats->app_rx_stall_min_ = std::numeric_limits<uint64_t>::max();
}

} // namespace nicc
