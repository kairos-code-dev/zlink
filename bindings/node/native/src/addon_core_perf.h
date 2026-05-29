/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

static const size_t k_perf_metric_header_size = 29;

uint64_t perf_now_ns();
bool perf_stamp_payload(void *payload,
                        size_t payload_size,
                        uint32_t run_id,
                        uint8_t phase,
                        uint32_t msg_size,
                        uint64_t seq);
bool perf_decode_payload_header(zlink_msg_t *parts,
                                size_t part_count,
                                uint32_t run_id,
                                uint32_t msg_size,
                                uint64_t *sent_ts_ns);
void perf_set_double_prop(napi_env env,
                          napi_value obj,
                          const char *name,
                          double value);
