[← 목차](./README.ko.md)

# 2. 시작하기

게임 생성 API 하나를 가진 최소 서버를 만들어 실행까지 확인한다.

## 1. CMake 연동

```cmake
find_package(zlink CONFIG REQUIRED)

add_executable(match_api src/main.cpp)
target_link_libraries(match_api PRIVATE zlink::framework)
```

소스에서는 facade header 하나를 include한다.

```cpp
#include <zlink/framework.hpp>
```

## 2. 메시지 정의

채널/HTTP로 오가는 데이터는 `packet_name`을 가진 평범한 struct다. HTTP(JSON)
경로는 nlohmann ADL 함수(`to_json`/`from_json`)로 직렬화를 연결한다.

```cpp
struct create_game_http_req_t
{
    static constexpr const char *packet_name = "CreateGameHttpReq";
    std::string game_name;
};

struct create_game_http_res_t
{
    static constexpr const char *packet_name = "CreateGameHttpRes";
    std::string room_id;
    std::string game_name;
};

inline void from_json (const nlohmann::json &json, create_game_http_req_t &value)
{
    value.game_name = json.value ("gameName", "");
}

inline void to_json (nlohmann::json &json, const create_game_http_res_t &value)
{
    json = nlohmann::json{{"roomId", value.room_id}, {"gameName", value.game_name}};
}
```

## 3. 핸들러 작성

여기서 만드는 것은 **노드 핸들러** — 채널·HTTP 요청을 처리하는 유형이다. 상속
없는 평범한 클래스로 세 가지 멤버가 계약이다.

- `using request_type` / `using reply_type` — 메시지 타입
- `static constexpr const char *topic_name` — 메시지 식별자
- `reply_type handle (const request_type &)` — 처리 본체

```cpp
class create_game_http_handler_t
{
  public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    static constexpr const char *topic_name = "CreateGame";

    create_game_http_res_t handle (const create_game_http_req_t &request)
    {
        const auto name = request.game_name.empty () ? "quick-match" : request.game_name;
        return {"room-" + name, name};
    }
};
```

두 가지를 기억한다.

- **수명은 transient, 실행은 동시** — 인스턴스는 요청마다 새로 만들어지고,
  서로 다른 요청이 worker 풀에서 **동시에** 실행된다. 멤버 변수에 상태를 누적할
  수 없고, 핸들러를 싱글톤으로 등록해서도 안 된다. 상태를 어디에 둘지는
  [3장 §4.1](./03-concepts.ko.md)의 규칙을 따른다.
- 비동기 작업이 필요하면 반환 타입을 `task_t<reply_type>`으로 바꾸고 코루틴으로
  쓴다 — [3장 §5](./03-concepts.ko.md) 참고.

> SPOT 안의 게임 룸 상태를 처리하는 SPOT 핸들러는 구조가 다르다 —
> `spot_t`/`entry_spot_t`를 상속하고 `configure()`로 등록하는 패턴이며
> [3장 §4.2](./03-concepts.ko.md)와 [8장](./08-spot.ko.md)에서 다룬다.

## 4. 앱 조립과 실행

`app_t::create()`로 앱을 만들고, `add_zlink_framework` 람다 안에서 토폴로지를
선언한 뒤 `run`을 호출한다.

```cpp
#include <zlink/framework.hpp>

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    app.logging ().use_file ("match-api.log");

    app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &options) {
        options.http ()
          .listen ("http://0.0.0.0:8080")
          .map_readiness ("/ready")
          .map_post<create_game_http_handler_t> ("/games");
    });

    return app.run (argc, argv);   // 블로킹: 종료 신호까지 서비스
}
```

`run`은 호스트를 시작하고 종료 요청(`stop()`/`request_stop()` 또는 프로세스
신호)까지 블로킹한다. 반환값은 프로세스 종료 코드다.

## 5. 동작 확인

```bash
$ ./match_api &

# readiness — map_readiness로 매핑한 경로
$ curl -s http://127.0.0.1:8080/ready
{"status":"healthy","readiness":"healthy","liveness":"healthy"}

# 게임 생성
$ curl -s -X POST http://127.0.0.1:8080/games \
       -H 'content-type: application/json' \
       -d '{"gameName":"ranked-match-0611"}'
{"roomId":"room-ranked-match-0611","gameName":"ranked-match-0611"}
```

## 6. 다음 단계

- 채널 메시징(서버 간 request-reply)을 붙이려면:
  ```cpp
  options.handlers ().add<authenticate_player_handler_t> ("api");
  options.codecs ().add_message_pack ().add_message_pack<authenticate_player_req_t> ();
  options.add_client_server_channel ("tictactoe.api")
    .enable_server ("tcp://0.0.0.0:5555")
    .use_handler_group ("api");
  ```
  자세한 내용은 7장(채널 메시징)에서 다룬다.
- endpoint·포트를 하드코딩하지 않고 설정으로 빼는 방법은
  [4. Configuration](./05-configuration.ko.md).
- 실제로 동작하는 전체 구성은 `samples/TicTacToe`가 정본이다 —
  `run_sample.sh`로 서버(Api·Play)와 클라이언트를 한 번에 띄울 수 있다.

[다음: 핵심 개념 →](./03-concepts.ko.md)
