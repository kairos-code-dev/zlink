# 5. 실행 모델

> [공통 계약 목차](README.ko.md)
>
> **terminator 계약은 이 문서가 소유하지 않는다.** terminator 축과 Spot 실행 문맥 결합(turn seam)은
> [12 HTTP client §3](12-http-client.ko.md)과
> [04 비동기 실행 정책 §1.1](../04-async-execution-policy.ko.md)이 소유한다. **어긋나면 그쪽이
> 이긴다.** 아래 §5.1은 그 계약을 **다시 적은 요약**이며, 이 문서의 몫은 §5.2 이하의 **HTTP 전송
> 실행 세부**(non-blocking 근거, 취소, timeout 경계)다.

## 5.1 두 실행 방식과 callback

HTTP client는 one-way submission과 response completion을 제공한다. 정확한 terminator 이름은
.NET `Async`, Kotlin wrapper `await`, Java·C++ `submit`이다. Node는 raw response `submitRaw`,
typed response·callback `async`와 one-way `submit`을 구분한다. Shared Spot gate를 반납하는
`Yield`는 HTTP request builder가 아니라 서버 request와 worker allowlist가 소유한다.

| 실행 방식 | 무엇을 기다리나 | Spot 실행 줄 |
| --- | --- | --- |
| **one-way submission** | queue admission 완료 | **turn을 유지한다.** 정상 완료 값은 없다 |
| **response completion** | HTTP response | **turn을 유지한다.** handler는 하나의 turn이다 |

**callback은 별도 완료 경로다.** awaitable을 쓰지 않는 호출자가 사용하며, 완료 callback은
Spot 실행 줄의 **새 turn**으로 들어간다
([framework 12 §3](12-http-client.ko.md)).

제출은 언어의 표준 비동기 값을 돌려주며, 네트워크 대기 중 호출자의 스레드/event loop를 점유하지
않는다.

| 언어 | 비동기 반환형 | non-blocking 근거 |
| --- | --- | --- |
| cpp | `task_t<T>` (`co_await`) | `.coroutines()` 활성 시 execute scheduler로 오프로드 |
| dotnet | `ValueTask<T>` | `SocketsHttpHandler` epoll/IOCP |
| java | `CompletionStage<T>` | `java.net.http` NIO selector |
| kotlin | `suspend` 함수 | java 런타임 + `CompletionStage.await()` 브리지 |
| node | `Promise<T>` | undici libuv |

**terminator 이름은 framework 관용을 따른다.** `.NET`은 `Async(...)`, Kotlin wrapper는
`await(...)`, Java·C++는 `submit(...)`을 사용한다. Node HTTP typed response와 callback은 TypeScript
상속 signature 충돌을 피하기 위해 `async(...)`를 유지하고 raw response는 `submitRaw()`를 사용한다
([04 §2](../04-async-execution-policy.ko.md)).

## 5.2 외부 HTTP 대기와 Spot 실행 줄

HTTP request builder는 shared Spot gate를 반납하는 `yield`를 제공하지 않는다. Actor 입·퇴장 시 외부
API를 기다리면서 room 전체와 timer를 진행해야 하면 I/O worker에서 HTTP response completion
terminator를 실행하고 worker call의 `Yield`로 기다린다.

```
var profile = await Context
    .RunIoWorker(async workerCancellation =>
        await http.Get($"/players/{id}").Async<Profile>(workerCancellation))
    .Yield(ct);
```

- **worker `Yield` 앞뒤로 spot 상태의 불변식을 가정하지 않는다.** 그 줄을 넘으면 다른 callback이
  상태를 바꿀 수 있다.
- **response completion terminator는 turn을 유지한다.** Spot 상태를 비동기 대기 전후에
  이어서 다뤄야 하면 이 terminator를 쓴다.

## 5.3 turn seam — execution scheduler 주입

**HTTP client는 framework를 모른다.** Spot의 turn을 아는 것은 **주입된 execution scheduler**
하나다. 바이너리 의존은 `framework → HTTP client` 한 방향을 유지한다.

- HTTP client는 **execution scheduler 주입점**을 공개 계약으로 둔다. scheduler가 completion을
  어디서 재개할지 정한다.
- **framework가 DI 등록 시 callback completion scheduler를 꽂는다.** callback은 Spot 실행 줄의 새
  turn으로 들어간다.
- DI와 단독 사용 모두 HTTP request builder에 **`yield`를 노출하지 않는다.**

cpp의 `coroutines(resume_scheduler)` / `framework_resume_scheduler_t`가 이 seam의 선례다.

- `coroutines()` — 기본 스케줄러 사용.
- `coroutines(resume_scheduler)` — 재개 위치 주입(framework 실행 줄에서 continuation 재개).
- `coroutines(execute_scheduler, resume_scheduler)` — 실행/재개 모두 주입.

주의(현행 구현 특성, 계약 아님): cpp 기본 스케줄러는 단일 스레드를 execute/resume 공용으로 쓰므로
요청이 직렬화되며, 재개된 continuation에서 같은 스케줄러의 다른 task를 blocking 대기하면 데드락이
가능하다.

## 5.4 blocking terminator를 두지 않는다

**완료 값을 동기로 언래핑하는 public terminator를 만들지 않는다.** 같은 의미의 blocking 대안
terminator는 계약 위반이다([04 §2](../04-async-execution-policy.ko.md)).

- 금지 대상: cpp `fetch()`/`.result()`, dotnet `Fetch<T>()`, java `fetch()`/`.join()`/`.get()`.
- 테스트나 CLI에서 동기로 기다려야 하면 **호출자가** 언어 관용으로 감싼다
  (`GetAwaiter().GetResult()`, `runBlocking`, `.join()`).
- 합성은 `co_await` / `await` / `thenCompose` / suspend로 한다.

## 5.5 서버 표면과 client 수명

**서버(Spot handler·channel handler)에서 쓰는 client는 DI로 주입받는다.** handler 안에서 정적
팩토리로 client를 만들지 않는다 — 연결 pool과 turn seam을 잃는다.

| 표면 | 누가 쓰나 | terminator |
|------|-----------|------------|
| 정적 팩토리 | CLI · client 시나리오 | response completion / callback |
| **DI 주입 client** | **Spot handler · 서버 코드** | one-way / response completion / callback |

- client는 서비스당 하나를 만들어 재사용한다(pool/keep-alive 이득).
- builder verb 단축(one-shot)은 제출 시 client를 lazy build하고 완료 후 닫는 **편의 경로**다.
  요청마다 전송 스택 초기화 비용을 내므로 반복/고부하 호출에 쓰지 않는다. one-shot 요청 객체는
  재제출할 수 없다(재제출 시 `requestProtocolError`).

## 5.6 취소

- dotnet은 제출에 `CancellationToken`을 받는다.
- kotlin coroutine 취소의 하부 요청 전파는 현재 미구현이다(개정 후보
  [R5](10-revision-candidates.ko.md)).
- cpp/java/node는 요청 단위 취소 API를 노출하지 않는다(timeout으로 경계).
