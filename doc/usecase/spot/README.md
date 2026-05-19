# SPOT 활용 시나리오

> 이 폴더는 SPOT의 활용 가능성을 탐색하는 아이디어 모음이다.
> 확정된 사례가 아니며, 실제 구축 경험을 통해 검증/보완이 필요하다.

## SPOT의 핵심 특성

| 특성 | SPOT | 외부 브로커 (Redis, Kafka 등) |
|------|------|---------------------------|
| 인프라 | 없음 (프로세스 내 임베디드) | 별도 서버 운영 필요 |
| 메시지 경로 | 노드 간 직접 mesh | 브로커를 경유 (홉 추가) |
| 장애 영향 | 해당 노드만 영향 | 브로커 장애 시 전체 중단 (SPOF) |
| 토폴로지 변경 | Discovery가 자동 조정 | 설정 변경 또는 클러스터 재구성 |
| 지연 | 낮음 (직접 전달) | 브로커 경유로 상대적 높음 |
| 내구성 | 없음 (live pub/sub) | 있음 (Kafka 등) |

**SPOT이 맞는 경우:** 외부 인프라 의존 없이, 저지연으로, 노드 간 실시간 데이터를
토픽 기반으로 공유해야 할 때.

**SPOT이 안 맞는 경우:** 메시지 내구성(durable), exactly-once, 과거 메시지
재전송이 필요한 경우 → Kafka 등이 적합.

## 시나리오 목록

| 시나리오 | SPOT 적합도 | 파일 |
|---------|:---------:|------|
| MMORPG 존 인접 데이터 공유 | **높음** | [01-mmorpg-zone.md](./01-mmorpg-zone.md) |
| 실시간 시세 분배 | **높음** | [02-realtime-quote.md](./02-realtime-quote.md) |
| 분산 메트릭 수집 | 보통 | [03-distributed-metrics.md](./03-distributed-metrics.md) |
| 마이크로서비스 이벤트 전파 | 보통 | [04-microservice-event.md](./04-microservice-event.md) |
| 배치 기반 채널 메시징 (채팅) | 보통 | [05-batched-channel-chat.md](./05-batched-channel-chat.md) |
