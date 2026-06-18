[← 목차](README.ko.md)

# 9. 인증과 TLS

## Basic / Bearer

```csharp
ZLinkHttpClient.Create("https://api.internal").BasicAuth("user", "secret").Build();
ZLinkHttpClient.Create("https://api.internal").BearerToken("eyJhbGci...").Build();
```

둘 다 `Authorization` 헤더를 설정한다. redirect 시 `Authorization`은 **same-origin
에서만 보존**되고 cross-origin으로는 제거된다([10장](10-redirects-retries-cookies.ko.md)).

## HTTPS / TLS 검증

기본적으로 시스템 신뢰 저장소로 서버 인증서를 검증한다. 테스트 인증서(self-signed)를
신뢰하려면 `TrustCertificateFile(path)`로 PEM 인증서를 지정한다. 이 인증서를 custom
root trust로 사용해 검증한다.

```csharp
ZLinkHttpClient.Create("https://localhost:8443")
    .TrustCertificateFile("test-ca.pem")
    .Build();
```

## mTLS client certificate

```csharp
ZLinkHttpClient.Create("https://mtls.internal")
    .ClientCertificateFile("client-cert.pem", "client-key.pem")
    .Build();
```

PEM 인증서와 키로 client 인증서를 구성해 `SslClientAuthenticationOptions`에 싣는다.

[다음: Redirect · Retry · Cookie →](10-redirects-retries-cookies.ko.md)
