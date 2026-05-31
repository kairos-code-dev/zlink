[← 서비스](./03-services.ko.md) · [Go 가이드](./index.ko.md) · [다음: 레퍼런스 →](./05-reference.ko.md)

# 운영

소켓 옵션, TLS, 모니터링, 폴러/타이머, 스레딩, 네이티브 라이브러리를 다룹니다.

---

## 소켓 옵션

소켓별 옵션 퍼사드를 통해 설정합니다. ([코어 참고](../../12-socket-options.ko.md))

```go
opts := socket.Options()

// 공통 옵션
opts.SetSendHWM(1000)                       // 전송 고수위 표시
opts.SetRecvHWM(1000)                       // 수신 고수위 표시
opts.SetSendTimeout(500 * time.Millisecond) // 전송 타임아웃
opts.SetRecvTimeout(500 * time.Millisecond) // 수신 타임아웃
opts.SetLinger(0)                           // Close() 후 대기 없음

// DEALER/ROUTER 전용
dealerOpts := dealer.Options()
dealerOpts.SetRequestTimeout(2 * time.Second)

routerOpts := router.Options()
routerOpts.SetMandatory(true) // 없는 라우팅 ID로 보내면 에러
```

자동 HWM(auto-hwm) 프로파일로 메모리 크기를 자동 관리합니다:

```go
ctxOpts := ctx.Options()
ctxOpts.SetAutoHwmEnabled(true)
ctxOpts.SetAutoHwmProfile(zlink.AutoHwmProfileBalanced)
```

---

## TLS 보안

서버와 클라이언트 양쪽에 인증서를 설정합니다. ([코어 참고](../../05-tls-security.ko.md))

```go
// 서버
socket.SetTLSServer("cert.pem", "key.pem", false) // requireClientCert=false

// 클라이언트
socket.SetTLSClient("ca.pem", "server-hostname", false) // trustSystem=false
```

`tls+tcp://` 트랜스포트로 연결합니다:

```go
server.Bind("tls+tcp://0.0.0.0:5556")
client.Connect("tls+tcp://server.example.com:5556")
```

---

## 모니터링

소켓의 연결 수명 이벤트를 구독합니다. ([코어 참고](../../06-monitoring.ko.md))

```go
// 기본: 모든 이벤트 구독
mon := socket.MonitorOpen()
defer mon.Close()

// 특정 이벤트만 구독
mon := socket.MonitorOpenWith(zlink.MonitorEventConnectionReady | zlink.MonitorEventPeerWeightChanged)

// 이벤트 수신 (블로킹)
var event zlink.MonitorEvent
mon.Recv(&event, zlink.RecvFlagsNone)

if event.IsConnectionReady() {
    fmt.Println("피어 연결됨")
}

// 콜백 방식
mon.SetHandler(func(e zlink.MonitorEvent) {
    fmt.Printf("event: %v addr: %s\n", e.Event(), e.LocalAddr())
})
```

---

## 폴러 / 타이머

여러 소켓과 파일 디스크립터를 동시에 폴링합니다.

```go
poller, err := zlink.NewPoller()
if err != nil { ... }
defer poller.Close()

// 소켓 등록 (슬롯 번호는 이벤트 식별용)
poller.Add(socket1, 1, zlink.PollIn)
poller.Add(socket2, 2, zlink.PollIn)
poller.AddFD(fileFD, 3, zlink.PollIn)

// 타임아웃까지 대기
events := make([]zlink.PollEvent, 16)
n, err := poller.Wait(events, 100*time.Millisecond)
for i := 0; i < n; i++ {
    switch events[i].Slot() {
    case 1:
        // socket1 수신 준비됨
    case 2:
        // socket2 수신 준비됨
    }
}
```

타이머:

```go
timer, err := zlink.NewTimer()
if err != nil { ... }
defer timer.Close()

timer.Start(500*time.Millisecond, 0) // 0 = 무한 반복

// 콜백 방식
timer.SetHandler(func(count uint64) {
    fmt.Printf("타이머 %d회 발화\n", count)
})

// 폴러와 함께 사용
poller.Add(timer, 99)
```

SpotNode 이벤트 루프에 바인딩된 타이머:

```go
timer, err := zlink.NewTimerFromSpot(spot)
```

---

## 스레딩

Go 바인딩의 스레딩 규칙입니다. ([코어 참고](../../11-thread-safety.ko.md))

| 항목 | 규칙 |
|------|------|
| `Context` | 고루틴 간 공유 가능 |
| 소켓 | **하나의 고루틴에서만** 사용. 동시에 여러 고루틴이 접근하면 경쟁 조건 발생 |
| `Recv` 콜백 | 네이티브 I/O 스레드가 아닌 Go 관리 고루틴에서 호출됨 |
| `Message.Data()` | 메시지가 살아 있는 고루틴 내에서만 접근 |

```go
// 올바른 패턴: 소켓 per-고루틴
go func() {
    socket, _ := ctx.PairSocket()
    defer socket.Close()
    socket.Connect(...)
    // 이 고루틴에서만 socket 사용
}()

// 잘못된 패턴: 두 고루틴이 동일 소켓 접근
go func() { socket.Recv(...) }()  // 위험
go func() { socket.Send(...) }()  // 위험
```

---

## 네이티브 라이브러리

Go 바인딩은 플랫폼별 `.so`(Linux) 또는 `.dylib`(macOS)를 내장합니다. 별도 설치
없이 `go get`으로 사용할 수 있습니다.

사용 중인 네이티브 버전 확인:

```go
v := zlink.RuntimeVersion()
fmt.Printf("zlink %d.%d.%d\n", v.Major, v.Minor, v.Patch)
```

특정 기능 지원 여부 확인:

```go
if zlink.Has("draft") {
    fmt.Println("draft API 지원")
}
```
