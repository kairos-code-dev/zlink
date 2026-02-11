# 성능 특성 및 튜닝 가이드

## 1. 벤치마크 결과

### 소형 메시지 (64B) 처리량

| 패턴 | TCP | IPC | inproc |
|------|-----|-----|--------|
| DEALER↔DEALER | 6.03 M/s | 5.96 M/s | 5.96 M/s |
| PAIR | 5.78 M/s | 5.65 M/s | 6.09 M/s |
| PUB/SUB | 5.76 M/s | 5.70 M/s | 5.71 M/s |
| DEALER↔ROUTER | 5.40 M/s | 5.55 M/s | 5.40 M/s |
| ROUTER↔ROUTER | 5.03 M/s | 5.12 M/s | 4.71 M/s |

> 표준 libzmq 대비 **~99% 처리량 동등성**.

## 2. WS/WSS 최적화 결과

| 메시지 크기 | WS 개선율 | WSS 개선율 |
|------------|-----------|-----------|
| 64B | +11% | +13% |
| 1KB | +50% | +37% |
| 64KB | +97% | +54% |
| 262KB | +139% | +62% |

### Beast 라이브러리 단독 대비

| Transport | Beast | zlink | 비율 |
|-----------|-------|-------|------|
| tcp | 1416 MB/s | 1493 MB/s | 105% |
| ws | 540 MB/s | 696 MB/s | 129% |

> WS/WSS 내부 최적화 상세(Copy elimination, Gather write)는 [STREAM 소켓 최적화](../internals/stream-socket.md)를 참고.

## 3. 메시지 크기별 처리량 가이드라인

| 메시지 크기 | 특성 | 권장 사항 |
|------------|------|-----------|
| ≤33B | VSM (inline) | 메모리 할당 없음. 최고 처리량 |
| 34B~1KB | LMSG (소형) | 일반적 성능. 복사 오버헤드 미미 |
| 1KB~64KB | LMSG (중형) | zero-copy (`zlink_msg_init_data`) 고려 |
| >64KB | LMSG (대형) | zero-copy 필수. WS/WSS는 Gather write 활용 |

### VSM 활용

33바이트 이하 메시지는 `msg_t` 구조체 내부에 직접 저장되어 **malloc 없이** 처리된다.

```c
/* VSM: 33B 이하 → 인라인 저장, 최고 효율 */
zlink_send(socket, "small msg", 9, 0);

/* LMSG: 34B 이상 → heap 할당 */
char large[1024];
zlink_send(socket, large, sizeof(large), 0);
```

프로토콜 설계 시 자주 교환되는 메시지는 33B 이내로 유지하면 처리량이 극대화된다.

## 4. Transport별 성능 특성

| Transport | 상대 성능 | 지연시간 | 오버헤드 | 추천 용도 |
|-----------|-----------|---------|----------|-----------|
| inproc | ★★★★★ | 최저 | 없음 | 스레드 간 통신 |
| ipc | ★★★★☆ | 낮음 | 시스템콜 | 로컬 프로세스 간 |
| tcp | ★★★★☆ | 네트워크 | TCP 스택 | 서버 간 통신 |
| ws | ★★★☆☆ | 네트워크 | WebSocket 프레이밍 | 웹 클라이언트 |
| tls/wss | ★★★☆☆ | 네트워크 | 암호화 + 프레이밍 | 보안 필요 시 |

### Transport별 오버헤드 분석

```
inproc:  Lock-free pipe 직접 연결. 시스템콜 없음.
ipc:     Unix 도메인 소켓. TCP 스택 우회.
tcp:     TCP/IP 스택. Nagle 비활성화로 지연 최소화.
ws:      tcp + WebSocket 프레이밍(2~14B 헤더). Binary mode.
wss/tls: ws/tcp + TLS 암호화. 핸드셰이크 + 레코드 오버헤드.
```

## 5. I/O 스레드 수 설정 가이드

```c
void *ctx = zlink_ctx_new();
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);
```

| I/O 스레드 | 추천 사용 사례 | 기준 |
|------------|---------------|------|
| 1 | 소규모 연결 (<100), 단순 패턴 | CPU 코어 1개 사용 |
| 2 (기본) | 일반적 사용 | 대부분의 시나리오에 적합 |
| 4 | 대규모 연결, 높은 처리량 | CPU 코어 4개 이상 |
| 코어 수 | 최대 처리량 | 전용 서버 |

### I/O 스레드 증가 시점

- 소켓 수 × 평균 메시지 레이트가 단일 스레드 처리량을 초과할 때
- 다수의 네트워크 연결(>100)을 동시 처리할 때
- WS/WSS 등 프레이밍 오버헤드가 큰 transport를 다량 사용할 때

### 주의사항

- I/O 스레드는 컨텍스트 생성 후, 소켓 생성 **전에** 설정
- inproc transport는 I/O 스레드를 사용하지 않음 (직접 파이프 연결)
- I/O 스레드를 과도하게 늘리면 컨텍스트 스위칭 오버헤드 발생

## 6. HWM (High Water Mark) 설정 가이드

```c
int hwm = 10000;
zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));
zlink_setsockopt(socket, ZLINK_RCVHWM, &hwm, sizeof(hwm));
```

| 설정 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_SNDHWM` | 1000 | 송신 큐 최대 메시지 수 |
| `ZLINK_RCVHWM` | 1000 | 수신 큐 최대 메시지 수 |

### 메모리 vs 처리량 트레이드오프

| HWM 값 | 메모리 사용 | 처리량 | 메시지 유실 |
|--------|-----------|--------|:----------:|
| 100 | 낮음 | 낮음 (빈번한 블록) | PUB: 빈번한 드롭 |
| 1000 (기본) | 보통 | 보통 | 균형 |
| 10000 | 높음 | 높음 (버스트 흡수) | PUB: 드롭 감소 |
| 100000 | 매우 높음 | 최대 | 메모리 주의 |

### HWM 동작 패턴

| 소켓 | HWM 초과 시 동작 |
|------|-----------------|
| PUB | 메시지 **드롭** (Slow Subscriber 보호) |
| DEALER | **블록** (기본) 또는 `EAGAIN` (`ZLINK_DONTWAIT`) |
| ROUTER | `ROUTER_MANDATORY` 시 `EHOSTUNREACH`, 아니면 드롭 |
| PAIR | **블록** (기본) 또는 `EAGAIN` |

### 메모리 계산

```
예상 메모리 = SNDHWM × 평균_메시지_크기 × 연결_수

예: HWM=10000, 메시지=1KB, 연결=100
    = 10000 × 1KB × 100 = ~1GB
```

## 7. 소켓 옵션 튜닝 체크리스트

| 옵션 | 기본값 | 튜닝 포인트 |
|------|--------|-------------|
| `ZLINK_LINGER` | -1 (무한) | 테스트: 0, 프로덕션: 1000~5000ms |
| `ZLINK_SNDTIMEO` | -1 (무한) | 응답 시간 요구사항에 맞춰 설정 |
| `ZLINK_RCVTIMEO` | -1 (무한) | 폴링 루프에서 사용 시 설정 |
| `ZLINK_SNDHWM` | 1000 | 처리량에 맞춰 조정 |
| `ZLINK_RCVHWM` | 1000 | 처리량에 맞춰 조정 |
| `ZLINK_MAXMSGSIZE` | -1 (무제한) | STREAM 소켓에서 보안 설정 |

### LINGER 설정

```c
/* 테스트 환경: 즉시 종료 */
int linger = 0;
zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));

/* 프로덕션: 미전송 메시지 대기 */
int linger = 3000;  /* 3초 */
zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));
```

### 타임아웃 설정

```c
/* 송신 타임아웃: 1초 후 EAGAIN */
int timeout = 1000;
zlink_setsockopt(socket, ZLINK_SNDTIMEO, &timeout, sizeof(timeout));

/* 수신 타임아웃: 500ms 후 EAGAIN */
int timeout = 500;
zlink_setsockopt(socket, ZLINK_RCVTIMEO, &timeout, sizeof(timeout));
```

## 8. 성능 측정 방법

### 기본 처리량 측정

```c
#include <time.h>

int count = 100000;
struct timespec start, end;
clock_gettime(CLOCK_MONOTONIC, &start);

for (int i = 0; i < count; i++) {
    zlink_send(socket, data, size, 0);
}

clock_gettime(CLOCK_MONOTONIC, &end);
double elapsed = (end.tv_sec - start.tv_sec) +
                 (end.tv_nsec - start.tv_nsec) / 1e9;

printf("처리량: %.2f msg/s\n", count / elapsed);
printf("처리량: %.2f MB/s\n", (count * size) / elapsed / 1e6);
```

### 지연시간 측정 (Ping-Pong)

```c
/* 클라이언트 */
clock_gettime(CLOCK_MONOTONIC, &start);
zlink_send(socket, "ping", 4, 0);
zlink_recv(socket, buf, sizeof(buf), 0);
clock_gettime(CLOCK_MONOTONIC, &end);

double rtt_us = ((end.tv_sec - start.tv_sec) * 1e6 +
                 (end.tv_nsec - start.tv_nsec) / 1e3);
printf("RTT: %.1f us\n", rtt_us);
```

## 9. 성능 체크리스트

### 기본 설정

- [ ] I/O 스레드 수를 워크로드에 맞게 설정
- [ ] HWM을 예상 처리량에 맞게 조정
- [ ] LINGER를 적절히 설정 (테스트: 0, 프로덕션: 타임아웃)

### 메시지 최적화

- [ ] 소형 메시지(≤33B)는 VSM 활용 (inline 저장)
- [ ] 대용량 메시지는 zero-copy (`zlink_msg_init_data`) 활용
- [ ] 상수 데이터는 `zlink_send_const()` 사용
- [ ] 불필요한 `zlink_msg_copy()` 회피

### Transport 최적화

- [ ] 로컬 통신은 inproc/ipc 사용
- [ ] 암호화가 불필요한 내부 통신은 tcp 사용
- [ ] WS/WSS 사용 시 메시지 크기별 성능 특성 고려

### 모니터링

- [ ] 성능 병목 시 모니터링 API로 연결 상태 확인
- [ ] Slow Subscriber 감지 (PUB/SUB 환경)
- [ ] HWM 도달 빈도 관찰

> Speculative I/O, Gather Write 등 내부 최적화 메커니즘의 상세는 [architecture.md](../internals/architecture.md)를 참고.
