<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework Spring Boot Monitoring](./spring-boot-monitoring.ko.md) | [다음: ZLink Framework Spring Boot SPOT](./spring-boot-spot.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](./README.ko.md)

[Java 묶음](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# ZLink Framework Spring Boot Registry

## 1. Embedded registry

같은 프로세스 안에서 registry를 띄우는 표면이 필요하다. `.NET`의
`AddZLinkRegistry(...)`에 대응하는 Java 표면은 framework channel 등록과 분리한다.
Registry만 띄우는 host는 `@EnableZLinkFramework` 없이도 만들 수 있어야 한다.

```java
@Configuration
public class RegistryConfig {
    @Bean
    ZLinkEmbeddedRegistryOptions zlinkEmbeddedRegistryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint("tcp://0.0.0.0:5551");
        options.setRouterEndpoint("tcp://0.0.0.0:5552");
        return options;
    }
}
```

Spring Boot starter는 이 option bean이 있으면 embedded registry lifecycle bean을
등록한다. option bean이 없으면 registry server를 만들지 않는다. 이 등록만으로는
framework lifecycle이나 channel client bean을 만들지 않는다. Framework runtime은
별도로 `@EnableZLinkFramework`를 붙인 설정에서 켠다.
Registry server의 시작과 종료도 Spring host lifetime이 맡는다. application이 registry
runtime을 직접 만들거나 `start` 함수로 시작하는 public facade는 두지 않는다.
application은 같은 프로세스 registry를 조회할 때도 runtime 객체가 아니라
`ZLinkRegistryQuery` bean을 사용한다. registry runtime 구현은 Spring lifecycle bean
뒤에 숨기며, public constructor나 public `start` 함수로 노출하지 않는다.

필수 option은 아래와 같다.

| Option | 의미 |
|--------|------|
| `registryId` | 운영 snapshot과 monitoring event에 표시할 registry id |
| `pubEndpoint` | service announcement를 publish하는 endpoint |
| `routerEndpoint` | query request를 받는 endpoint |
| `heartbeatInterval` | peer registry 로 heartbeat 를 보내는 주기 |
| `heartbeatTimeout` | peer registry heartbeat 만료로 보는 시간 |
| `broadcastInterval` | service 목록을 다시 broadcast 하는 주기 |
| `addPeer(...)` | 연결할 peer registry 의 pub endpoint 추가 |

`pubEndpoint`나 `routerEndpoint`가 비어 있으면 startup validation 오류다. Registry
host와 framework host가 같은 프로세스에 있더라도 registry option bean과
`@EnableZLinkFramework` framework 설정은 별도로 표현한다.

## 2. Query surface

- in-process query: `ZLinkRegistryQuery`
- remote query: `ZLinkRegistryQueryClient`

운영 화면이나 warm-up은 이 조회 표면으로 설명하는 편이 맞다.

embedded registry를 등록하면 `ZLinkRegistryQuery` bean도 함께 등록한다. Registry가
다른 프로세스에서 동작하면 `.NET`의 `AddZLinkRegistryQueryClient(...)`에 대응하는
별도 configurer를 사용한다.

```java
@Configuration
public class RegistryQueryClientConfig {
    @Bean
    ZLinkRegistryQueryClientCustomizer registryQueryClientCustomizer() {
        return options -> {
            options.setEndpoint("tcp://127.0.0.1:5552");
        };
    }
}
```

`ZLinkRegistryQuery`와 `ZLinkRegistryQueryClient`는 하나로 합치지 않는다. 전자는
같은 프로세스의 registry runtime snapshot을 읽고, 후자는 remote query protocol을
사용한다.

```java
public interface ZLinkRegistryQuery {
    CompletionStage<ZLinkRegistryStatus> status();
    CompletionStage<List<ZLinkRegistryServiceSummaryEntry>> serviceSummary(
        @Nullable ZLinkRegistryServiceSummaryFilter filter);
    CompletionStage<List<ZLinkRegistryTopologyEntry>> topology(
        @Nullable ZLinkRegistryTopologyFilter filter);
    CompletionStage<List<ZLinkMemberPeerEntry>> memberPeers(String channelName);
}

public interface ZLinkRegistryQueryClient extends AutoCloseable {
    CompletionStage<List<ZLinkRegistryTopologyEntry>> topology(
        @Nullable ZLinkRegistryTopologyFilter filter);
}
```

remote query client(`ZLinkRegistryQueryClient`)는 `topology(...)` 하나만 노출한다.
status, service summary, member peer 조회는 in-process `ZLinkRegistryQuery` 전용이다.

query client는 hidden retry를 하지 않는다. 연결 실패와 timeout은 호출자가 관찰할 수
있는 실패로 반환한다.

## 3. Discovery와의 관계

일반 request 핫패스는 각 channel의 discovery view를 기준으로 설명한다.
registry query는 운영 점검과 topology snapshot 용도로 분리하는 편이 맞다.

Actor/session binding은 Registry row로 저장하지 않는다. session은 actor id/type과
core ActorGateway가 제공하는 logical handle을 사용하고, Registry는 Spot owner 조회와
운영 topology 확인에 집중한다.

Spot remote address 기본값은 Registry-backed resolver로 제공할 수 있다. 이 resolver는
`spotRid`에서 user Spot route 정보를 찾는 용도이며, client session에서 actor route
snapshot을 직접 들고 있게 만드는 용도가 아니다.

## 4. Lifecycle

embedded registry와 framework runtime이 같은 Spring application 안에 함께 있으면
registry lifecycle이 먼저 시작한다. framework discovery client가 registry endpoint에
연결하기 전에 registry bind가 끝나야 하기 때문이다.

startup 순서는 아래와 같다.

1. registry option validation
2. registry backend context 생성
3. registry pub/router endpoint bind
4. `ZLinkRegistryQuery` bean ready
5. framework runtime start

shutdown은 반대 순서다. framework runtime을 먼저 멈추고 registry endpoint를 닫는다.
별도 registry process에서는 이 순서가 process 간 계약이 아니며, framework는 discovery
연결 실패를 runtime event로 올린다.

## 5. 검증 기준

- `pubEndpoint` 누락은 startup validation 오류다.
- `routerEndpoint` 누락은 startup validation 오류다.
- embedded registry 등록 시 `ZLinkRegistryQuery` bean이 resolve된다.
- remote query client 등록 시 `ZLinkRegistryQueryClient` bean이 resolve된다.
- remote query client는 topology snapshot을 읽을 수 있다.
- Registry와 framework를 함께 등록한 host에서 registry가 먼저 bind된다.
- request hot path는 `ZLinkRegistryQuery`를 직접 호출하지 않는다.
