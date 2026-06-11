[← 목차](./README.ko.md)

# 9. 인증과 TLS

## HTTP Basic

`basic_auth(user, password)`가 `Authorization: Basic <base64>`를 모든 요청에
싣는다.

```cpp
auto registry = zlink::http_client::client_t::create ("https://registry.example.internal")
                  .json ()
                  .basic_auth ("matchmaker-svc", service_password)
                  .build ();
```

## Bearer 토큰

`bearer_token(token)`이 `Authorization: Bearer <token>`을 싣는다. OAuth/JWT
기반 API에 쓴다.

```cpp
auto api = zlink::http_client::client_t::create ("https://game-api.example.internal")
             .json ()
             .bearer_token (access_token)
             .build ();
```

토큰이 만료로 갱신되는 서비스라면 client를 토큰 수명 단위로 재생성하거나,
요청 단위 `header("authorization", ...)`로 덮어쓴다.

```cpp
client.get ("/players/7281")
  .header ("authorization", "Bearer " + token_provider.current ())
  .submit<player_profile_t> ();
```

(proxy 인증은 [11. Proxy](./11-proxy.ko.md)의 `proxy_basic_auth` 참고.)

## HTTPS 기본 동작

`https://` endpoint는 항상 다음을 수행한다. **끄는 옵션은 없다** — 묵시적으로
TLS 검증을 비활성화하지 않는 것이 계약이다.

- TLS handshake
- server certificate 검증 (시스템 CA 경로 기본)
- hostname 검증

검증 실패(신뢰 안 되는 인증서, hostname 불일치)는 transport 실패로 보고된다.

## 사설/테스트 인증서 신뢰

내부 CA나 개발용 인증서는 `trust_certificate_file`로 **명시적으로** 신뢰를
추가한다.

```cpp
auto internal_api =
  zlink::http_client::client_t::create ("https://game-api.staging.internal:8443")
    .json ()
    .trust_certificate_file ("/etc/pki/staging-root-ca.pem")
    .build ();
```

## mTLS (client certificate)

서버가 클라이언트 인증서를 요구하면 `client_certificate_file(cert, key)`로
제시한다. PEM 형식이다.

```cpp
auto settlement =
  zlink::http_client::client_t::create ("https://settlement.partner-bank.example:9443")
    .json ()
    .trust_certificate_file ("/etc/pki/partner-bank-ca.pem")
    .client_certificate_file ("/etc/pki/matchmaker-client.crt.pem",
                              "/etc/pki/matchmaker-client.key.pem")
    .build ();
```

인증서를 제시하지 않으면 서버가 handshake를 거부하고 transport 실패로 끝난다.

## OpenSSL 없는 빌드

OpenSSL이 없는 빌드에서 `https://` 요청은
`request_protocol_error`("HTTPS support requires OpenSSL")로 닫힌다.
`http://`는 영향 없다.

[다음: Redirect · Retry · Cookie →](./10-redirects-retries-cookies.ko.md)
