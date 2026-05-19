[← MMORPG 존](./01-mmorpg-zone.md) | [목록](./README.md) | [메트릭 수집 →](./03-distributed-metrics.md)

# 실시간 시세 분배

## 문제

거래소 시세 데이터를 트레이딩 서버 N대에 실시간 분배해야 한다.
각 트레이딩 서버는 관심 종목만 수신하고, 리스크 서버는 전체를 수신한다.
브로커를 경유하면 지연이 늘어나고, 트레이딩 서버 증감 시 설정 변경이 필요하다.

## 구조

```
                                     ┌────────────┐
                         ┌──────────►│ Trading 1  │
                         │           │ "quote:AAPL"│
  ┌──────────┐    mesh   │           └────────────┘
  │ Gateway  │───────────┤           ┌────────────┐
  │ (시세수신) │           ├──────────►│ Trading 2  │
  └──────────┘           │           │ "quote:TSLA"│
    publish:             │           └────────────┘
    "quote:AAPL"         │           ┌────────────┐
    "quote:TSLA"         └──────────►│   Risk     │
    "quote:GOOG"                     │ "quote:*"  │
     ...                             └────────────┘
```

## 핵심 코드

```c
/* Gateway: 거래소 시세를 종목별 토픽으로 publish */
char topic[64];
snprintf(topic, sizeof(topic), "quote:%s", symbol);
zlink_publish(spot, topic, &price_msg, 1, 0);

/* Trading: 관심 종목만 구독 */
zlink_set_subscription(spot, "quote:AAPL");
zlink_set_subscription(spot, "quote:TSLA");

/* Risk: 전체 종목 구독 */
zlink_set_subscription(spot, "quote:*");
```

## 왜 SPOT인가

- 브로커 홉 없이 Gateway → Trading 직접 전달 → 지연 최소화
- Trading 서버 추가/제거 시 Gateway 코드 변경 없음
- 종목별 토픽 필터링으로 불필요한 시세 수신 방지

## 고려사항

- SPOT은 live pub/sub이므로 시세 유실 가능
- 유실 불가 요건이면 별도 시퀀스 번호 + 갭 감지 로직 필요
