[Go 가이드](./index.ko.md) · [다음: 메시징 →](./02-messaging.ko.md)

# 시작하기

이 문서는 모듈 추가부터 첫 메시지 송수신, 그리고 모든 기능이 공유하는 핵심 타입과
소유권 규칙까지 다룹니다. 메시징 개념 자체는 [코어 가이드](../../01-overview.ko.md)가
소유하며, 여기서는 Go 표면만 설명합니다.

---

## 설치

**`zlink.systems/zlink`** 모듈로 제공됩니다. 네이티브 코어가 플랫폼별로 번들됩니다.

```bash
go get zlink.systems/zlink
```

- **Go 1.22** 이상.
- 네이티브 별도 설치 불필요 — RID별 `.so`/`.dll`을 자동 로드합니다.

```go
import zlink "zlink.systems/zlink/contracts"
```

---

## 5분 예제 — PING/ACK

`Pair` 소켓으로 한쪽이 `PING`을 보내고 다른 쪽이 `ACK`로 답하는 최소 예제입니다.
서버는 bind, 클라이언트는 connect합니다.

```go
// 서버
ctx, _ := zlink.NewContext()
defer ctx.Close()

server, _ := ctx.PairSocket()
defer server.Close()

server.Bind("tcp://127.0.0.1:5555")

var received zlink.Received
if _, err := server.Recv(&received, zlink.RecvFlagsNone); err != nil { ... }
defer received.Close()

part, _ := received.SinglePartOrError()
fmt.Println(string(part.Data())) // PING

reply, _ := zlink.NewMessage([]byte("ACK"))
server.Send().Message(reply).Submit(nil)
```

```go
// 클라이언트
ctx, _ := zlink.NewContext()
defer ctx.Close()

client, _ := ctx.PairSocket()
defer client.Close()
client.Connect("tcp://127.0.0.1:5555")

ping, _ := zlink.NewMessage([]byte("PING"))
client.Send().Message(ping).Submit(nil)

var received zlink.Received
if _, err := client.Recv(&received, zlink.RecvFlagsNone); err != nil { ... }
defer received.Close()

part, _ := received.SinglePartOrError()
fmt.Println(string(part.Data())) // ACK
```

실제 코드에서는 반드시 에러를 확인합니다. 위 예제는 흐름을 보여주기 위해
`_`로 처리했습니다.

`Pair`는 가장 단순한 패턴(피어 1:1)이라 첫 프로그램에 적합합니다. 확장 가능한
요청/응답·팬아웃·파이프라인은 [메시징](./02-messaging.ko.md)을 참고하세요.

---

## 핵심 타입

모든 기능이 공유하는 4가지 기본 타입입니다.

### 1. 컨텍스트 (Context)

프로세스의 런타임 진입점입니다. 보통 하나만 만들고 모든 소켓·서비스를 여기서
생성합니다.

```go
ctx, err := zlink.NewContext()
if err != nil { ... }
defer ctx.Close() // 컨텍스트를 닫으면 하위 소켓이 모두 종료됩니다
```

I/O 스레드 수를 조정할 때는 `Options()`를 사용합니다:

```go
opts := ctx.Options()
opts.SetIOThreads(4)
```

> 소켓은 컨텍스트가 닫히기 **전에** 명시적으로 닫는 것을 권장합니다.
> 컨텍스트를 닫으면 열려 있는 소켓에 대한 블로킹 작업이 중단됩니다.
> ([스레딩](./04-operations.ko.md#스레딩) 참고)

### 2. 메시지 (Message)

페이로드 프레임 하나를 소유합니다. 전송하면 소유권이 이전되어 `Close()`를
별도로 호출할 필요가 없습니다. 전송에 실패하면 소유권이 유지되어 재시도하거나
명시적으로 닫아야 합니다.

```go
// 바이트 슬라이스에서 복사본 생성
msg, err := zlink.NewMessage([]byte("payload"))

// 크기를 지정해 빈 프레임 할당, 직접 채워 넣기
msg, err := zlink.NewMessageWithSize(256)
copy(msg.Data(), myData)

// 문자열에서 복사본 생성
msg, err := zlink.NewMessageString("payload")

// 전송 안 하고 폐기할 때
defer msg.Close()
```

수신된 메시지의 페이로드를 읽을 때는 `Data()`를 사용합니다. 반환된 슬라이스는
메시지 수명에 묶여 있으므로 필요하면 복사합니다:

```go
data := msg.Data()            // 메시지가 살아 있는 동안만 유효
snapshot := msg.Bytes()       // 독립 복사본
text := msg.Text()            // UTF-8 문자열 변환
```

### 3. Received — 수신 봉투

수신한 메시지 봉투입니다. 라우팅 ID, 파트 목록, 선택적 회신 컨텍스트를 담습니다.
미리 선언해 두고 여러 번 재사용할 수 있습니다. 파트는 `Close()`를 호출하면 해제됩니다.

```go
var received zlink.Received   // 스택에 선언, 힙 할당 없음
_, err = socket.Recv(&received, zlink.RecvFlagsNone)
defer received.Close()        // 수신된 파트 해제

// 단일 파트 접근
part, err := received.SinglePartOrError()
payload := part.Data()

// 멀티파트 접근
for _, part := range received.Parts() {
    _ = part.Data()
}

// 라우팅 ID (ROUTER/SPOT 수신 시)
rid := received.RoutingID() // *RoutingID, nil이면 없음
```

### 4. 라우팅 ID (RoutingID)

피어나 스팟을 식별하는 1~255 바이트의 불변 값입니다. 직접 생성하거나
수신 봉투에서 추출합니다.

```go
rid := zlink.NewRoutingID([]byte("server-01"))
rid := zlink.NewRoutingIDString("server-01")
rid := zlink.NewRoutingIDUint32(1)          // 4바이트 big-endian
rid := zlink.NewRoutingIDUUIDBytes(uuid)    // 16바이트 UUID
rid, err := zlink.ParseRoutingIDHex("0102...")   // 16진수 파싱

fmt.Println(rid.String())  // 사람이 읽기 좋은 형태로 출력
```

---

## 소유권 규칙

Go 바인딩의 소유권 규칙은 단순합니다.

| 상황 | 규칙 |
|------|------|
| `Submit` 성공 | 추가한 `*Message`의 소유권이 전송 스택으로 이전됩니다. `Close()` 불필요 |
| `Submit` 실패 | 소유권이 호출자에게 반환됩니다. `Close()` 필요 |
| `Recv` 성공 | 호출자가 `Received`의 소유권을 가집니다. `defer received.Close()` 필수 |
| `Request.SubmitAsync` 완료 | 회신 파트(`[]*Message`) 소유권이 호출자에게 옵니다. 각 파트를 `Close()` |
| `Context.Close()` | 컨텍스트 하위의 모든 블로킹 작업을 중단합니다 |

```go
// 패턴: 에러가 나도 안전하게
msg, _ := zlink.NewMessage([]byte("data"))
if _, err := socket.Send().Message(msg).Submit(nil); err != nil {
    defer msg.Close() // 전송 실패 시에만 닫음
}
// 전송 성공 시 msg는 이미 소비되어 Close() 불필요
```
