# draft: C++ 외부 완료 task (design candidate)

상태: **draft(설계 후보)** — public 계약 아님. spec 지원이 생기기 전에는 구현하지 않는다.

## 무엇이 없나

application 코드가 **자기가 만든 task를 나중에 다른 handler가 완료시키는** 방법이 C++ public
표면에 없다. 다른 언어는 런타임이 그 조각을 준다.

| 언어 | 쓰는 것 |
|---|---|
| .NET | `TaskCompletionSource<T>` (BCL) |
| Java/Kotlin | `CompletableFuture<T>` |
| Node | `Promise` + 외부 resolve |
| C++ | **없음** — `std::promise`는 await가 blocking이라 `task_t<T>`와 맞지 않는다 |

`zlink::framework::detail::task_completion_source_t<T>`가 `contracts/dispatch/task.hpp` 안에 있지만
`detail` namespace라 application이 쓸 표면이 아니다.

## 어디서 막히나 (실사례)

DeliveryDispatch 샘플의 배송원 offer(`SMP-DD-003`):

- `.NET` 정본은 courier actor가 delivery별 `TaskCompletionSource`를 들고 `OfferAsync`에서
  `await pending.Task`로 기다린다. 배송원의 결정이 `Complete()`로 그 task를 완료시킨다.
- C++은 그 await를 표현할 수단이 없어, actor turn **바깥**의 랑데부(`condition_variable`)에서
  worker thread를 **블로킹**해 기다린다. 동작은 하지만 handler thread 하나를 결정이 올 때까지 붙든다.

같은 모양은 "요청을 받아 두고, 나중에 도착하는 다른 메시지로 응답한다"는 흐름 어디서나 나온다.

## 후보 표면

```cpp
namespace zlink::framework {

template <typename T> class task_completion_source_t
{
  public:
    task_completion_source_t ();
    task_t<T> task ();
    void complete (result_t<T> result);   // 중복 완료는 기존 결과를 덮어쓰지 않는다
};

}
```

의미는 이미 spec이 `task_t<T>`에 대해 적어 둔 것과 같다(§8: "완료 상태는 한 번만 확정되며, 중복
완료 시도는 기존 결과를 덮어쓰지 않는다"). 즉 새 의미를 만드는 게 아니라, 그 의미를 만드는 쪽을
application에 여는 것이다.

## 결정이 필요한 것

1. 이 표면이 **framework public 계약**이 맞는가, 아니면 언어 런타임이 줘야 할 것인가.
   (C++에는 그 런타임이 없다는 점이 이 후보의 근거다.)
2. spec 어디에 선언하는가 — `languages/cpp/02-framework-interfaces.ko.md` §8(async 실행)이 자리로
   보인다. 공통 spec의 async-execution-policy에는 언어 중립 문구가 필요한지 판단이 필요하다.
3. 취소·timeout을 이 표면이 갖는가, 아니면 호출자의 request timeout에 맡기는가.

## 그때까지

`SMP-DD-003`은 "worker가 큐·선택 정책·offer timeout을 소유한다"까지 닫고, 노드의 blocking 랑데부는
남겨 둔다. 이 draft가 계약으로 승격되면 그 조각을 걷어낸다.
