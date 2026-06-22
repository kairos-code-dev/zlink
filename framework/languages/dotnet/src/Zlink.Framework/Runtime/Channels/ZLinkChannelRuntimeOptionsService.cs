using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.Runtime.Channels;

// live serving socket 을 backing 으로 하는 IZLinkSocketConfig. Weight 는 socket 에서 read/write 하고,
// startup 전용 속성은 read 는 build-time recipe 값을 돌려주되 set 은 명확한 오류로 거부한다.
// (build-time 객체는 ZLinkSocketConfig 가 그대로 처리하므로 이 wrapper 는 runtime 전용이다.)
internal sealed class ZLinkLiveSocketConfig(
    IZLinkBackendWeightedSocket socket,
    IZLinkSocketConfig recipe) : IZLinkSocketConfig
{
    // Weight set/get 는 admin 스레드 등 receive loop 와 다른 스레드에서 호출될 수 있다. native 옵션
    // 접근은 core 의 socket public-API lock(socket_public_api_lock_scope_t, setsockopt 경로)을 거치며,
    // 이는 framework 가 이미 caller 스레드에서 socket.Send 를 호출할 때 의존하는 것과 동일한 동시성
    // 경계다. 즉 weight mutation 은 기존 concurrent send 패턴보다 더 위험하지 않다.
    public int Weight
    {
        get => socket.GetPeerWeight();
        set
        {
            ZLinkSocketConfig.ValidatePeerWeight(value);
            socket.SetPeerWeight(value);
        }
    }

    public long MaxMessageSize
    {
        get => recipe.MaxMessageSize;
        set => throw StartupOnly(nameof(MaxMessageSize));
    }

    public int SendHighWaterMark
    {
        get => recipe.SendHighWaterMark;
        set => throw StartupOnly(nameof(SendHighWaterMark));
    }

    public int ReceiveHighWaterMark
    {
        get => recipe.ReceiveHighWaterMark;
        set => throw StartupOnly(nameof(ReceiveHighWaterMark));
    }

    public int SendBufferSize
    {
        get => recipe.SendBufferSize;
        set => throw StartupOnly(nameof(SendBufferSize));
    }

    public int ReceiveBufferSize
    {
        get => recipe.ReceiveBufferSize;
        set => throw StartupOnly(nameof(ReceiveBufferSize));
    }

    public TimeSpan? Linger
    {
        get => recipe.Linger;
        set => throw StartupOnly(nameof(Linger));
    }

    public TimeSpan? ReceiveTimeout
    {
        get => recipe.ReceiveTimeout;
        set => throw StartupOnly(nameof(ReceiveTimeout));
    }

    public TimeSpan? SendTimeout
    {
        get => recipe.SendTimeout;
        set => throw StartupOnly(nameof(SendTimeout));
    }

    public TimeSpan? ConnectTimeout
    {
        get => recipe.ConnectTimeout;
        set => throw StartupOnly(nameof(ConnectTimeout));
    }

    public TimeSpan? HandshakeInterval
    {
        get => recipe.HandshakeInterval;
        set => throw StartupOnly(nameof(HandshakeInterval));
    }

    public bool IPv6
    {
        get => recipe.IPv6;
        set => throw StartupOnly(nameof(IPv6));
    }

    public bool TcpNoDelay
    {
        get => recipe.TcpNoDelay;
        set => throw StartupOnly(nameof(TcpNoDelay));
    }

    public bool Immediate
    {
        get => recipe.Immediate;
        set => throw StartupOnly(nameof(Immediate));
    }

    private static ZLinkConfigurationException StartupOnly(string name)
    {
        return new ZLinkConfigurationException(
            $"'{name}' is a startup-only socket option and cannot be changed at runtime. "
            + "Only Weight is runtime-mutable; set startup options at build time via Configure*Socket().");
    }
}

// 확장 슬롯(routing/client socket/pubsub) 의 1차 미wiring 공통 오류.
internal static class ZLinkRuntimeOptionsErrors
{
    public static ZLinkConfigurationException NotWired(string aspect)
    {
        return new ZLinkConfigurationException(
            $"Runtime configuration of '{aspect}' is not supported in this release; "
            + "only the serving socket Weight (drain) is wired for runtime mutation.");
    }
}

internal sealed class ZLinkClientServerRuntimeOptions(IZLinkSocketConfig serverSocket)
    : IZLinkClientServerChannelOptions
{
    public IZLinkSocketConfig ConfigureServerSocket() => serverSocket;

    public IZLinkSocketConfig ConfigureClientSocket() => throw ZLinkRuntimeOptionsErrors.NotWired(nameof(ConfigureClientSocket));

    public IZLinkRouteConfig ConfigureServerRouting() => throw ZLinkRuntimeOptionsErrors.NotWired(nameof(ConfigureServerRouting));

    public IZLinkOutboundRouteConfig ConfigureClientRouting() => throw ZLinkRuntimeOptionsErrors.NotWired(nameof(ConfigureClientRouting));
}

internal sealed class ZLinkRouteMeshRuntimeOptions(IZLinkSocketConfig socket)
    : IZLinkRouteMeshChannelOptions
{
    public IZLinkSocketConfig ConfigureSocket() => socket;
}

internal sealed class ZLinkDealerMeshRuntimeOptions(IZLinkSocketConfig socket)
    : IZLinkDealerMeshChannelOptions
{
    public IZLinkSocketConfig ConfigureSocket() => socket;
}

internal sealed class ZLinkChannelRuntimeOptions(ZLinkFrameworkRuntime runtime)
    : IZLinkChannelRuntimeOptions
{
    public IZLinkClientServerChannelOptions ClientServerChannel(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        return new ZLinkClientServerRuntimeOptions(runtime.ResolveClientServerServerSocketConfig(channelName));
    }

    public IZLinkRouteMeshChannelOptions RouteMeshChannel(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        return new ZLinkRouteMeshRuntimeOptions(runtime.ResolveRouteMeshSocketConfig(channelName));
    }

    public IZLinkDealerMeshChannelOptions DealerMeshChannel(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        return new ZLinkDealerMeshRuntimeOptions(runtime.ResolveDealerMeshSocketConfig(channelName));
    }
}
