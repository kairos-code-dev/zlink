[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework API

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 아래 API 이름은 방향 설명을 위한 예시다.

## 1. 목적

같은 `ZLink Framework`라도 `ASP.NET Core`, `Spring`, `NestJS` 사용자가 기대하는 표면은
조금씩 다르다. 이 문서는 각 프레임워크에서 "어떤 식으로 보이면 자연스러운가"를
정리한다.

핵심 원칙은 단순하다.

- 프레임워크 사용자가 익숙한 등록 방식에 맞춘다.
- low-level socket 이름을 공용 API 앞면으로 내세우지 않는다.
- request handler, event handler, service client를 DI와 함께 설명한다.

## 2. 공통 방향

### 2.1 서버 쪽

- handler를 프레임워크 표준 등록 방식으로 붙인다.
- 요청 body는 typed object로 받는다.
- header metadata와 timeout 정보는 context에서 조회한다.

### 2.2 클라이언트 쪽

- service client를 DI로 주입한다.
- 요청 메서드는 async 중심으로 제공한다.
- codec, timeout, target service를 설정할 수 있다.
- gateway 주소나 load balancer 주소 대신 `service_name` 기준 호출을 기본으로
  삼는다.
- provider selection은 adapter 내부 정책으로 처리할 수 있어야 한다.
- 단순 unary request 외에 event publish와 필요하면 aggregate helper를 분리할 수
  있어야 한다.
- 운영 점검이나 관리 API에서는 Registry topology snapshot/query 결과를 읽는
  별도 surface를 둘 수 있어야 한다.
- `SPOT`을 쓰는 프레임워크에서는 publish/subscribe만이 아니라 필요 시 routed
  request/reply도 같은 큰 개념 안에서 설명할 수 있어야 한다.

## 3. ASP.NET Core 방향

### 3.1 기대하는 표면

- `AddZlinkMessaging(...)`
- `MapZlinkHandlers()` 또는 그와 비슷한 endpoint 등록
- typed client DI
- `SPOT` node / publisher / subscriber의 hosted lifecycle 통합

### 3.2 예시

```csharp
builder.Services.AddZlinkMessaging(options =>
{
    options.AddClient("api.profile");
});

app.MapZlinkHandlers();

public sealed class ProfileHandler
{
    [ZlinkHandler("profile.get")]
    public Task<ProfileReply> HandleAsync(ProfileRequest request, ZlinkContext ctx)
    {
        return Task.FromResult(new ProfileReply());
    }
}
```

보다 자세한 `.NET` 초안은 [dotnet/README.ko.md](./dotnet/README.ko.md)를 참고한다.

## 4. Spring 방향

### 4.1 기대하는 표면

Spring에서는 annotation 기반 handler가 자연스럽다.
RSocket의 `@MessageMapping`과 비슷한 경험을 주는 방향이 적합하다.

### 4.2 예시

```java
@ZlinkController
public final class ProfileController {

    @ZlinkMapping("profile.get")
    public Mono<ProfileReply> get(ProfileRequest request, ZlinkContext ctx) {
        return Mono.just(new ProfileReply());
    }
}
```

## 5. NestJS 방향

### 5.1 기대하는 표면

NestJS는 메시지 기반 프로그래밍 모델이 이미 익숙하므로, 가능하면
`@MessagePattern`, `@EventPattern` 같은 기존 감각과 닮게 가는 편이 좋다.

### 5.2 예시

```typescript
@Controller()
export class ProfileController {
  @MessagePattern('profile.get')
  getProfile(data: ProfileRequest, ctx: ZlinkContext): Promise<ProfileReply> {
    return Promise.resolve({} as ProfileReply);
  }

  @EventPattern('cache.invalidate')
  invalidate(data: InvalidateEvent, ctx: ZlinkContext): void {
  }
}
```

## 6. 아직 확정하지 않는 것

- 공용 annotation 이름을 프레임워크마다 통일할지
- NestJS에서 기존 decorator를 그대로 재사용할지, zlink 전용 decorator를 둘지
- ASP.NET Core에서 endpoint mapping과 attribute model 중 어디를 우선할지
- scatter-gather 같은 aggregate helper를 adapter 기본 기능으로 둘지
- workflow metadata를 context에 어느 수준까지 노출할지

지금 단계에서는 이름보다 "그 프레임워크 사용자가 낯설지 않게 느끼는가"를 더
중요하게 본다.
