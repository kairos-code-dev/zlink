/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <chrono>

namespace zlink::samples::deliverydispatch
{

/* 배차 시한은 DispatchWorker가 소유한다(공통 sample spec §5). offer 요청 시한이 지나면
 * 워커가 다음 배송원으로 재배차한다. courier 결정 시한은 그보다 짧아서, 배송원이 답하지
 * 않으면 노드가 먼저 "거절"로 응답을 닫고 워커가 재배차를 이어간다. */
struct sample_timings_t
{
    static constexpr std::chrono::milliseconds courier_decision_timeout{700};
    static constexpr std::chrono::milliseconds offer_request_timeout{5000};
};

} // namespace zlink::samples::deliverydispatch
