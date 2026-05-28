/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_MONITORING_H_INCLUDED
#define ZLINK_MONITORING_H_INCLUDED

#include <zlink/common.h>
#include <zlink/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

typedef void (*zlink_monitor_handler_fn) (
  const zlink_monitor_event_t *event_, void *userdata_);

typedef zlink_monitor_event_t zlink_socket_monitor_event_t;
typedef zlink_monitor_handler_fn zlink_socket_monitor_handler_fn;

typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;

/**
 * @brief Ignore socket monitor events while keeping a valid handler symbol.
 *
 * Pass this when you want snapshot or direct polling on the returned monitor
 * handle without automatic callback dispatch.
 */
ZLINK_EXPORT void zlink_monitor_ignore_handler (
  const zlink_monitor_event_t *event_, void *userdata_);

typedef struct zlink_monitor_status_t
{
    /* snapshot 대상 종류입니다. raw socket, SPOT pub, SPOT sub 중 하나입니다. */
    zlink_monitor_source_kind_t source_kind;

    /* 현재 상태 비트입니다. READY, BOUND_READY, CLOSED 등을 담습니다. */
    zlink_monitor_state_mask_t state_flags;

    /* 어떤 세부 값이 채워졌는지 나타내는 비트마스크입니다. */
    zlink_monitor_status_detail_mask_t detail_flags;

    /* 현재 송신 큐에 남아 있는 메시지 수입니다. */
    uint64_t snd_pending_msgs;

    /* 현재 수신 큐에 남아 있는 메시지 수입니다. 일부 source에서는 근사값입니다. */
    uint64_t rcv_pending_msgs;

    /* 자동 HWM 정책이 이 source에 적용 중이면 1, 아니면 0입니다. */
    uint32_t auto_hwm_enabled;

    /* 현재 자동 HWM profile 값입니다. zlink_auto_hwm_profile_t 값과 같습니다. */
    uint32_t auto_hwm_profile;

    /* 자동 HWM 계산에 사용한 socket 역할입니다. 진단용 값입니다. */
    uint32_t auto_hwm_role;

    /* 역할과 socket type에서 정해진 자동 HWM 정책 분류입니다. */
    uint32_t auto_hwm_policy_class;

    /* 이 socket의 메시지 슬롯 계산에 사용한 단위 예산입니다. */
    uint64_t auto_hwm_unit_budget_bytes;

    /* profile과 정책 분류에서 정한 메시지 슬롯 상한입니다. */
    uint32_t auto_hwm_size_cap;

    /* 단위 예산과 메시지 크기로 계산한 메시지 슬롯 수입니다. */
    uint64_t auto_hwm_socket_message_slots;

    /* 자동 HWM 계산에 사용한 메시지 크기입니다. 단위는 byte입니다. */
    uint64_t auto_hwm_effective_message_bytes;

    /* 현재 socket에 적용된 송신 HWM입니다. */
    int32_t auto_hwm_applied_sndhwm;

    /* 현재 socket에 적용된 수신 HWM입니다. */
    int32_t auto_hwm_applied_rcvhwm;

    /* 현재 socket에 적용된 송신 buffer 크기입니다. 단위는 byte입니다. */
    int32_t auto_hwm_effective_sndbuf;

    /* 현재 socket에 적용된 수신 buffer 크기입니다. 단위는 byte입니다. */
    int32_t auto_hwm_effective_rcvbuf;

    /* 마지막 자동 HWM 재계산 시각입니다. 단위는 millisecond입니다. */
    uint64_t auto_hwm_last_recalc_ms;

    /* 마지막 자동 HWM 재계산 사유입니다. ZLINK_AUTO_HWM_RECALC_REASON_* 값입니다. */
    uint32_t auto_hwm_last_recalc_reason;

    /* 송신 시도 중 backpressure로 막힌 비율입니다. 단위는 ppm입니다. */
    uint32_t auto_hwm_send_blocked_ratio_ppm;

    /* HWM 축소가 지연 중이면 목표 송신 HWM, 없으면 -1입니다. */
    int32_t auto_hwm_deferred_sndhwm;

    /* HWM 축소가 지연 중이면 목표 수신 HWM, 없으면 -1입니다. */
    int32_t auto_hwm_deferred_rcvhwm;
} zlink_monitor_status_t;

/**
 * @brief Open and return a socket monitor handle directly.
 * @param events_  Event bitmask.
 * @return Monitor handle, or NULL on failure.
 */
ZLINK_EXPORT void *zlink_socket_monitor_open (
  void *s_, const zlink_socket_monitor_open_options_t *options_);

ZLINK_EXPORT zlink_handler_result_t zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT zlink_recv_result_t zlink_socket_monitor_recv (
  void *monitor_,
  zlink_socket_monitor_event_t *out_,
  zlink_recv_flags_t flags_);

/** @brief Read the current snapshot for a monitor handle. */
ZLINK_EXPORT zlink_config_result_t zlink_monitor_status (void *monitor_,
                                         zlink_monitor_status_t *out_);

ZLINK_EXPORT zlink_close_result_t zlink_monitor_close (void **monitor_p_);


#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;

typedef struct zlink_poller_event_t
{
    zlink_poller_source_kind_t source_kind;
    void *socket;
    zlink_fd_t fd;
    void *timer;
    void *user_data;
    short events;
} zlink_poller_event_t;

#ifndef ZLINK_HAVE_POLLER
#define ZLINK_HAVE_POLLER 1
#endif

ZLINK_EXPORT int zlink_poll (zlink_pollitem_t *items_,
                             int nitems_,
                             long timeout_,
                             zlink_config_result_t *error_out_);

ZLINK_EXPORT void *zlink_poller_new (void);
ZLINK_EXPORT zlink_close_result_t zlink_poller_destroy (void **poller_p_);
ZLINK_EXPORT int zlink_poller_size (void *poller_,
                                    zlink_config_result_t *error_out_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_add (void *poller_,
                                   void *socket_,
                                   void *user_data_,
                                   short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_modify (void *poller_,
                                      void *socket_,
                                      short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_remove (void *poller_, void *socket_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_add_fd (void *poller_,
                                      zlink_fd_t fd_,
                                      void *user_data_,
                                      short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_add_timer (void *poller_,
                                         void *timer_,
                                         void *user_data_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_modify_fd (void *poller_,
                                         zlink_fd_t fd_,
                                         short events_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_);
ZLINK_EXPORT zlink_config_result_t zlink_poller_remove_timer (void *poller_, void *timer_);
ZLINK_EXPORT int zlink_poller_wait (void *poller_,
                                    zlink_poller_event_t *events_,
                                    int n_events_,
                                    long timeout_,
                                    zlink_config_result_t *error_out_);
/******************************************************************************/
/*  Timers                                                                    */
/******************************************************************************/

typedef void (*zlink_timer_handler_fn) (void *timer_,
                                        uint64_t fire_count_,
                                        void *userdata_);

ZLINK_EXPORT void *zlink_timer_new (void);
ZLINK_EXPORT void *zlink_spot_timer_new (void *spot_);
ZLINK_EXPORT zlink_close_result_t zlink_timer_destroy (void **timer_p_);
ZLINK_EXPORT zlink_config_result_t zlink_timer_start (void *timer_,
                                    uint64_t interval_ns_,
                                    uint64_t repeat_count_);
ZLINK_EXPORT zlink_config_result_t zlink_timer_stop (void *timer_);
ZLINK_EXPORT zlink_recv_result_t zlink_timer_recv (void *timer_,
                                   uint64_t *fire_count_out_);
ZLINK_EXPORT zlink_handler_result_t zlink_timer_handler (void *timer_,
                                      zlink_timer_handler_fn handler_,
                                      void *userdata_);

#ifdef __cplusplus
}
#endif

#endif
