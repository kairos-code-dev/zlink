[← 목차](README.ko.md)

# 11. Proxy

## HTTP proxy

`Proxy(url)`로 HTTP proxy를 지정한다. URL은 `http://`로 시작해야 한다. 내부적으로
`WebProxy`로 핸들러에 구성된다.

```csharp
ZLinkHttpClient.Create("https://api.internal")
    .Proxy("http://proxy.internal:3128")
    .Build();
```

## proxy 인증

```csharp
ZLinkHttpClient.Create("https://api.internal")
    .Proxy("http://proxy.internal:3128")
    .ProxyBasicAuth("proxy-user", "proxy-secret")
    .Build();
```

`ProxyBasicAuth`는 `Proxy-Authorization: Basic ...` 헤더를 구성한다.

[다음: 압축 →](12-compression.ko.md)
