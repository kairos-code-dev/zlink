<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Spring Boot Monitoring](./spring-boot-monitoring.ko.md) | [다음: Draft -- ZLink Framework Spring Boot SPOT](./spring-boot-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Spring Boot Registry

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 Registry를 어떤 표면으로 통합할지
> 정리한다.

## 1. Embedded registry

같은 프로세스 안에서 registry를 띄우는 표면이 필요하다.

```java
@Configuration
public class RegistryConfig {
    @Bean
    ZLinkRegistryCustomizer registryCustomizer() {
        return options -> {
            options.setPubEndpoint("tcp://0.0.0.0:5551");
            options.setRouterEndpoint("tcp://0.0.0.0:5552");
        };
    }
}
```

## 2. Query surface

- in-process query: `ZLinkRegistryQuery`
- remote query: `ZLinkRegistryQueryClient`

운영 화면이나 warm-up은 이 조회 표면으로 설명하는 편이 맞다.

## 3. Discovery와의 관계

일반 request 핫패스는 각 channel의 discovery view를 기준으로 설명한다.
registry query는 운영 점검과 topology snapshot 용도로 분리하는 편이 맞다.
