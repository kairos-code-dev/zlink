[← 목차](./README.ko.md)

# 3. 핵심 개념

프레임워크 전반에서 반복되는 다섯 가지 — app 수명주기, 구성 표면, DI, 핸들러
모델, 실행 모델 — 을 정리한다.

## 1. app_t 수명주기

```text
create() ──▶ 구성 단계 ──▶ run(argc, argv) ──▶ 서비스 중 ──▶ 종료
             config/logging/                     stop() 또는
             add_zlink_framework/                request_stop()
             add_hosted_service
```

```cpp
auto app = zlink::framework::app_t::create ();
// ... 구성 ...
return app.run (argc, argv);
```

- **구성 단계** — `run` 전에 모든 선언을 끝낸다. 잘못된 구성(중복 라우트,
  빈 endpoint 등)은 구성 시점이나 `run` 시작에서 예외로 거부된다.
- **`run`** — 블로킹. 채널 바인딩, HTTP listener, hosted service 시작을 끝낸 뒤
  종료 요청까지 서비스한다. 반환값이 종료 코드다.
- **종료** — `request_stop()`은 비동기 요청(신호 핸들러 등에서 호출),
  `stop()`은 동기 정지다. 종료 시 hosted service `stop()` → 채널/HTTP 정리
  순서로 내려간다.

백그라운드 작업은 `hosted_service_t`로 수명주기에 편입시킨다.

```cpp
class season_scheduler_t : public zlink::framework::hosted_service_t
{
  public:
    void start (zlink::framework::service_provider_t &services) override
    {
        _store = &services.get_required<season_store_t> ();
        _worker = std::thread ([this] { run_schedule (); });
    }
    void stop () noexcept override
    {
        _running = false;
        if (_worker.joinable ()) _worker.join ();
    }
    // ...
};

app.add_hosted_service (std::make_unique<season_scheduler_t> ());
```

## 2. 구성 표면 지도

`app_t`에는 역할별 진입점이 나뉘어 있다.

| 진입점 | 역할 | 다루는 장 |
|--------|------|-----------|
| `app.config()` | 설정 소스 로딩과 조회 | [4장](./04-configuration.ko.md) |
| `app.logging()` | 로그 출력 대상 (`use_file(...)` 등) | 11장 |
| `app.monitoring()` / `app.metrics()` / `app.health()` | 관측·상태 | 11장 |
| `app.add_zlink_framework(람다)` | **zlink 토폴로지 선언** — 채널/SPOT/stream/HTTP/registry | 5~10장 |
| `app.add_module(...)` / `add_zlink_framework<TModule>()` | 구성 패키징 (아래 §6) | — |
| `app.advanced()` | services/handlers/zlink builder 직접 접근 (탈출구) | — |

`add_zlink_framework` 람다가 받는 `zlink_framework_options_t`가 기능 선언의
중심이다.

```cpp
app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
    options.services ()...;     // DI 등록
    options.handlers ()...;     // 핸들러 그룹 등록
    options.codecs ()...;       // 직렬화 codec 등록
    options.add_client_server_channel (...);   // 채널
    options.add_spot_node (...);               // SPOT
    options.add_stream_node (...);             // stream
    options.http ()...;                        // HTTP hosting
    options.use_discovery ()...;               // registry 연동
});
```

## 3. 서비스(DI) 컨테이너

`service_collection_t`에 등록하고 `service_provider_t`로 꺼낸다.

```cpp
options.services ().add_singleton<sample_topology_t> (
  std::make_unique<sample_topology_t> (topology));
```

| 등록 | 수명 |
|------|------|
| `add_singleton<T>()` / `add_singleton<T>(instance)` | 앱 전체에서 1개 |
| `add_scoped<T>()` | scope(`service_scope_t`) 안에서 1개, scope마다 새로 |
| `add_transient<T>()` | resolve할 때마다 새 인스턴스 |

소비 측은 `get_required<T>()`로 받는다. 핸들러는 **`dependency_types` 선언 +
생성자 주입**이 표준 패턴이다.

```cpp
class create_game_http_handler_t
{
  public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t,
                                          sample_topology_t,
                                          zlink::framework::logger_t<create_game_http_handler_t>>;
    static constexpr const char *topic_name = "CreateGame";

    explicit create_game_http_handler_t (zlink::framework::channel_client_t &client,
                                         sample_topology_t &topology,
                                         zlink::framework::logger_t<create_game_http_handler_t> &logger);
    // ...
};
```

`dependency_list_t`에 적은 순서대로 컨테이너가 resolve해 생성자에 넣어 준다.
`channel_client_t`, `logger_t<T>` 같은 프레임워크 서비스도 같은 방식으로 받는다.

## 4. 핸들러 모델

모든 인바운드 처리(채널, HTTP, SPOT, actor)는 같은 모양의 핸들러로 수렴한다.

```cpp
class authenticate_player_handler_t
{
  public:
    using request_type = authenticate_player_req_t;
    using reply_type = authenticate_player_res_t;
    static constexpr const char *topic_name = "AuthenticatePlayer";

    authenticate_player_res_t handle (const authenticate_player_req_t &request);
};
```

- **동기 핸들러** — `reply_type handle(const request_type&)`
- **코루틴 핸들러** — `task_t<reply_type> handle(const request_type&)`,
  내부에서 `co_await`로 다른 호출을 기다린다
- **핸들러 그룹** — `options.handlers().add<T>("api")`로 그룹에 등록하고,
  채널이 `use_handler_group("api")`로 그룹을 가져다 쓴다. 같은 핸들러 묶음을
  여러 채널에서 재사용할 수 있다.
- **수명은 transient** — 핸들러 인스턴스는 요청마다 새로 만들어진다. 멤버에
  상태를 누적하지 말고, 공유 상태는 싱글톤 서비스로 주입받는다.
- 메시지 디코딩 → 핸들러 호출 → 응답 인코딩은 런타임이 처리한다. 핸들러는
  타입 있는 DTO만 본다.

## 5. 실행 모델: task_t와 result_t

프레임워크 전반의 비동기 값은 `task_t<T>`, 성공/실패는 `result_t<T>`로
표현된다. [http_client 가이드의 실행 모델](../../http-client/doc/07-async-coroutines.ko.md)과
동일한 모델이다.

```cpp
// 코루틴 핸들러에서 다른 서버 호출을 기다리기
zlink::framework::task_t<create_game_http_res_t>
handle (const create_game_http_req_t &request)
{
    auto room = co_await _client
                  .request<create_game_res_t> ("tictactoe.play",
                                               create_game_req_t{request.game_name})
                  .async ();
    co_return create_game_http_res_t{room.room_id, room.game_name};
}
```

규칙은 하나다 — **런타임(핸들러) 스레드에서는 `co_await`, blocking
(`.result()`)은 테스트·클라이언트 시나리오에서만.** 실패는 `co_await` 경로에서
`framework_exception_t`(`kind()`/`is_retriable()`)로 던져지고, `result_t`
경로에서는 `error()`로 조회한다.

코루틴 핸들러를 돌리는 worker 수는 `options.handler_coroutine_workers(n)`으로
조정한다.

## 6. 구성 패키징: module_t

기능 단위 구성을 재사용하려면 `module_t`를 구현해 묶는다. 서비스·zlink
토폴로지·핸들러·모니터링 구성을 한 단위로 배포할 수 있다.

```cpp
class matchmaking_module_t : public zlink::framework::module_t
{
  public:
    void configure_services (zlink::framework::service_collection_t &services) override;
    void configure_zlink (zlink::framework::zlink_builder_t &zlink) override;
    void configure_handlers (zlink::framework::handler_registry_t &handlers) override;
};

app.add_zlink_framework<matchmaking_module_t> ();   // 또는 app.add_module (module);
```

[다음: Configuration →](./04-configuration.ko.md)
