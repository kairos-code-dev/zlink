# .NET messaging local bench 규격

이 문서는 로컬 개발 머신에서 gRPC .NET, ZLink framework .NET, ZLink binding .NET의
상대 비용을 간단히 비교하기 위한 기준이다. 운영 환경의 mesh, TLS, L7 load balancer,
multi-node 분배, 네트워크 지연을 대표하지 않는다.

## 1. 비교 범위

처음 범위는 세 시나리오와 두 payload 크기만 사용한다. `request/reply`와 `command`는
단일 task로 순서대로 보내는 경우와 여러 task가 동시에 보내는 경우를 모두 본다. 비교
대상은 아래 세 구현이다.

| 구현 이름 | 의미 |
|-----------|------|
| `grpc-dotnet` | ASP.NET Core gRPC unary RPC |
| `zlink-framework-dotnet` | framework channel messaging의 client/server channel |
| `zlink-dotnet` | framework를 거치지 않는 raw .NET binding의 DEALER/ROUTER TCP 경로 |

| 시나리오 | gRPC .NET | ZLink framework .NET | ZLink binding .NET | 해석 |
|----------|-----------|----------------------|--------------------|------|
| request/reply sequential | unary RPC | `RequestToChannel(...).Async<TReply>()` | `Dealer.Send()` 후 `Dealer.Recv()` | `inflight=1`로 요청 하나의 echo 왕복 비용 확인 |
| request/reply concurrent | unary RPC | `RequestToChannel(...).Async<TReply>()` | slot별 `Dealer.Send()` 후 `Dealer.Recv()` | `inflight=N`개 요청을 동시에 유지할 때의 request 처리량 |
| command | unary RPC returning empty reply | `SendToChannel(...).Submit()` | slot별 `Dealer.Send().Submit()` | 응답 payload가 없는 command를 `inflight=1`과 `inflight=N`으로 나누어 비교 |

payload 크기는 `1 KiB`, `4 KiB`만 표준으로 본다. payload 크기는 protobuf `bytes body`의
전체 크기다. 앞 29 bytes는 측정 header로 사용하고, 나머지를 business payload 영역으로 채운다.
gRPC HTTP/2 frame, protobuf field overhead, ZLink envelope, ZMP header는 이 크기에 포함하지
않는다.

## 2. 실행 조건

- client process 1개와 비교 대상별 server process 1개로 실행한다. 로컬 비교 runner는
  gRPC server, ZLink framework server, ZLink raw binding server를 각각 띄운다.
- loopback 주소(`127.0.0.1`)만 사용한다.
- Release build로 실행한다.
- warmup 뒤 정해진 시간의 measured active 구간을 실행한다.
- `request/reply`와 `command` 모두 gRPC와 ZLink에서 같은 `inflight` 값을 사용한다. 기본값은
  `1,100`이다.
- gRPC와 ZLink framework는 같은 protobuf DTO를 사용한다. ZLink binding은 같은 bytes payload를
  protobuf envelope 없이 보낸다.
- ZLink는 location store 없이 manual endpoint 연결을 사용한다.
- ZLink binding의 request echo endpoint와 command 수신 endpoint는 분리한다. command 측정에서
  reply 없는 단방향 수신량을 보려면 request echo reply가 같은 socket에 섞이면 안 되기 때문이다.
- TLS, compression, service mesh, gateway, broker는 사용하지 않는다.

## 3. 메트릭

필수 출력은 아래 메트릭이다. 처리량 단위는 시나리오 성격에 맞춰 분리한다.

| 메트릭 | 의미 |
|--------|------|
| `Throughput` | measured 구간 처리량. 표에서는 `4.031 KOPS`, `183.618 KMSG/s`처럼 값과 단위를 한 칸에 함께 표시 |
| `Bandwidth` | payload 크기와 처리량으로 계산한 전송량. perf와 같이 `MB/s`로 표시 |
| `Lat.Mean(ms)` | 평균 latency. request/reply는 client 왕복 latency, command는 server가 header로 계산한 수신 latency |
| `Lat.P95(ms)` | p95 latency |
| `Lat.P99(ms)` | p99 latency |
| `errors` | 실패한 호출 수 |

보고서와 콘솔 출력은 perf runner에서 다루기 쉽게 metric별 `RESULT,current,...` 형식도
같이 남긴다. `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`를
각각 별도 줄로 출력한다.

`request/reply`는 echo reply가 돌아온 완료 수를 기준으로 `KOPS`를 계산한다. 여기서
`1 KOPS`는 초당 1,000건의 request/reply 완료를 뜻한다.

`request/reply sequential`은 `inflight=1`이다. 한 요청의 reply가 오기 전에는 다음 요청을
보내지 않으므로, 단일 요청 왕복 latency를 보기 쉽다.

`request/reply concurrent`는 `inflight=N`이다. client는 active phase 동안 항상 최대 `N`개의
미완료 request를 유지한다. reply 하나가 도착하면 그 slot에서 다음 request를 즉시 보낸다.
measured active 시간이 끝나면 새 request를 더 보내지 않고 이미 보낸 request의 reply를 기다린 뒤
완료 수를 active 시간으로 나누어 `KOPS`를 계산한다. 이 값이 실제 server 간 사용에 더 가까운
처리량이다.

`command`도 `inflight=1`과 `inflight=N`을 따로 본다. `inflight=1`은 한 task가 순서대로
command를 보내고, `inflight=N`은 `N`개 task/slot이 동시에 command를 보낸다. 처리량은
server가 active phase에서 받은 메시지 수를 기준으로 `KMSG/s`를 계산한다. 여기서
`1 KMSG/s`는 초당 1,000개 메시지를 뜻한다. ZLink `send`는 reply를 기다리지 않으므로
client의 `Submit()` 호출 수만으로 처리량을 계산하지 않는다. gRPC empty unary는 HTTP/2
request/response 완료를 기다리므로, 두 값은 "응답 없는 command 경로"라는 별도 항목으로
보고 `request/reply` 결과와 섞어 평균 내지 않는다.

## 4. 측정 payload header

측정 payload 앞에는 `bindings/c/perf`의 metric header와 같은 의미의 29-byte header를
넣는다. 이 header는 payload 본문 일부이며, protobuf `bytes body`의 앞부분에 들어간다.

| offset | 크기 | 값 |
|--------|------|----|
| 0 | 4 | magic `0x5A4C4E4B` (`ZLNK`) |
| 4 | 4 | run id |
| 8 | 1 | phase (`0` warmup, `1` active) |
| 9 | 4 | payload size |
| 13 | 8 | sequence |
| 21 | 8 | send timestamp ns |

`request/reply`는 reply payload에 돌아온 header를 client가 검증하고, active phase reply
수로 `KOPS`를 계산한다. `command`는 server가 header를 읽어 active phase message 수와
server-side 수신 latency를 계산한다. 이 방식은 `bindings/c/perf`처럼 payload 안에 측정
값을 넣어, client stopwatch만으로 단방향 처리량을 과장하지 않기 위한 기준이다.

## 5. 결과 해석

이 bench는 작은 로컬 비교 도구다. 결과 문서나 guide에 성능 우위 문장을 넣으려면 아래 정보를
함께 남긴다.

- CPU, OS, .NET SDK version
- commit hash
- payload size
- warmup과 active duration 설정
- gRPC와 ZLink endpoint
- request inflight 값
- command inflight 값
- 결과 JSON 원본

결과는 "이 조건에서 더 빠르다/느리다"로만 해석한다. 일반적인 운영 성능이나 모든 payload에서의
우위를 주장하지 않는다.
