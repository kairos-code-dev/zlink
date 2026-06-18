[← 시세 분배](02-realtime-quote.md) | [목록](README.md) | [이벤트 전파 →](04-microservice-event.md)

# 분산 메트릭/이벤트 수집

## 문제

클러스터 내 각 서비스 노드가 CPU, 메모리 등 메트릭을 발행하고,
대시보드/알림 서비스가 수집해야 한다.
Prometheus/StatsD 같은 별도 수집 인프라 없이 가볍게 구성하고 싶다.

## 구조

```
  ┌──────────┐                       ┌─────────────┐
  │ Service A│── "metrics:A:cpu" ──►│             │
  └──────────┘                       │  Dashboard  │
  ┌──────────┐                       │ "metrics:*" │
  │ Service B│── "metrics:B:mem" ──►│             │
  └──────────┘                       └─────────────┘
  ┌──────────┐                       ┌─────────────┐
  │ Service C│── "metrics:C:cpu" ──►│   Alerter   │
  └──────────┘                       │  (cpu 필터)  │
                                     └─────────────┘
```

## 핵심 코드

```c
/* 각 서비스: 자기 메트릭을 주기적으로 publish */
char topic[64];
snprintf(topic, sizeof(topic), "metrics:%s:cpu", my_service_name);
zlink_publish(spot, topic, &cpu_msg, 1, 0);

/* Dashboard: 전체 수집 */
zlink_set_subscription(spot, "metrics:*");

/* Alerter: 콜백에서 cpu 메트릭만 필터링 */
zlink_set_subscription(spot, "metrics:*");
```

## 왜 SPOT인가

- 메트릭 수집용 별도 인프라 없이 동작
- 이미 zlink을 사용하는 시스템이면 추가 의존성 없음
- 서비스 증감에 mesh가 자동 대응

## 고려사항

- 시계열 저장이 필요하면 콜백에서 DB/파일로 기록하는 계층 필요
- 대규모 클러스터에서는 메트릭 볼륨에 따른 mesh 트래픽 주의
