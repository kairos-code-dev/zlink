[← 목차](./README.ko.md)

# 7. 비동기와 코루틴

`submit_raw()`/`submit<T>()`는 `zlink::framework::task_t`를 돌려준다. 결과를
소비하는 방법은 세 가지다.

## co_await (framework handler 안에서의 표준)

framework runtime/handler 코드는 `co_await`로 받는다. 성공이면 값이 나오고,
실패면 `framework_exception_t`가 던져진다.

```cpp
zlink::framework::task_t<void>
notify_match_result (zlink::http_client::client_t &client, const match_result_t &result)
{
    auto response = co_await client.post ("/matches/" + result.match_id + "/result")
                      .body (result)
                      .submit<ack_res_t> ();
    // response: http_response_t<ack_res_t>
    if (response.body.accepted) {
        co_return;
    }
    throw zlink::framework::framework_exception_t (
      zlink::framework::framework_error_kind_t::request_failed,
      "match result was not accepted");
}
```

## blocking: .result() / fetch<T>()

`task.result()`는 결과가 올 때까지 호출 스레드를 멈추고 `result_t`를 돌려준다.
`fetch<T>()`는 거기에 더해 래퍼를 풀고 실패를 예외로 바꾼다.

```cpp
auto result = client.get ("/leaderboard").submit<leaderboard_t> ().result ();
auto board = client.get ("/leaderboard").fetch<leaderboard_t> ();   // 동등 + 언래핑
```

## 콜백 submit

`submit<T>(callback)`은 완료 시 `result_t`를 콜백으로 전달한다.

```cpp
client.post ("/games")
  .body (create_game_req_t{.name = "ranked-match-0611"})
  .submit<create_game_res_t> ([] (const auto &result) {
      if (!result) {
          log_error (result.error ()->what ());
          return;
      }
      on_game_created (result.value ().body);
  });
```

## 어디서 무엇을 쓰나 — blocking 규칙

> **framework runtime/handler 스레드에서는 blocking 접근(`.result()`,
> `fetch<T>()`)을 쓰지 않는다.** runtime 스레드를 멈추면 같은 스레드에서 처리될
> 다른 작업까지 막힌다.

| 호출 위치 | 권장 |
|-----------|------|
| framework handler / actor / spot 코드 | `co_await submit<T>()` |
| 테스트 코드 | `fetch<T>()` 또는 `.result()` |
| client 시나리오·CLI·배치 | `fetch<T>()` 또는 `.result()` |
| 콜백 스타일이 자연스러운 glue 코드 | `submit<T>(callback)` |

## 알아 둘 것: 실행은 동기다

현재 구현에서 HTTP 교환 자체는 `submit` 호출 안에서 동기로 완료되고, `task_t`는
완료된 결과를 감싼다. 즉 `co_await`가 "기다리는 동안 다른 일"을 만들어 주지는
않는다. 이 규칙(`handler에서는 co_await`)은 **미래에 실행이 진짜 비동기로 바뀌어도
호출 코드가 그대로 유효하도록** 하는 계약이다. non-blocking 실행은 별도 설계
범위다 — [1. 개요](./01-overview.ko.md).

[다음: Streaming →](./08-streaming.ko.md)
