[← 메트릭 수집](03-distributed-metrics.md) | [목록](README.md) | [채널 메시징 →](05-batched-channel-chat.md)

# 마이크로서비스 이벤트 전파

## 문제

서비스 간 도메인 이벤트를 비동기로 전파해야 한다.
주문 생성 시 결제/재고/알림 서비스가 각각 반응해야 하지만,
서비스 간 직접 호출은 결합도를 높인다.

## 구조

```
  ┌──────────┐   "order:created"    ┌──────────┐
  │  Order   │─────────────────────►│ Payment  │
  │ Service  │                      └──────────┘
  └──────────┘─────────────────────►┌──────────┐
                                    │Inventory │
               ─────────────────────►└──────────┘
                                    ┌──────────┐
               ─────────────────────►│  Notify  │
                                    └──────────┘
```

## 핵심 코드

```c
/* Order Service: 주문 이벤트 발행 */
zlink_publish(spot, "order:created", &order_msg, 1, 0);
zlink_publish(spot, "order:cancelled", &cancel_msg, 1, 0);

/* Payment Service: 주문 생성만 구독 */
zlink_set_subscription(spot, "order:created");

/* Notify Service: 모든 주문 이벤트 구독 */
zlink_set_subscription(spot, "order:*");
```

## 왜 SPOT인가

- 서비스 간 직접 이벤트 전달, 메시지 큐 인프라 불필요
- 새 서비스 추가 시 구독만 하면 됨 (기존 서비스 변경 없음)
- 같은 프로세스 내 zlink 소켓과 혼용 가능 (DEALER/ROUTER로 RPC + SPOT으로 이벤트)

## 고려사항

- 이벤트 유실 시 비즈니스 영향이 큰 경우 SPOT은 부적합
  (예: 결제 이벤트 유실 → 주문 미처리)
- 이런 경우 Kafka/RabbitMQ 등 durable 보장이 있는 시스템이 적합
- SPOT은 "유실되어도 다음 이벤트로 복구 가능한" 경우에 맞음
