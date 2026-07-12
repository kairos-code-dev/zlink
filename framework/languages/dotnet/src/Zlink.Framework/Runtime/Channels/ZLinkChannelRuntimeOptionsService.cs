using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Runtime.Channels;

// live serving socket 을 backing 으로 하는 IZLinkSocketConfig. Weight 는 socket 에서 read/write 하고,
// startup 전용 속성은 read 는 build-time recipe 값을 돌려주되 set 은 명확한 오류로 거부한다.
// (build-time 객체는 ZLinkSocketConfig 가 그대로 처리하므로 이 wrapper 는 runtime 전용이다.)
internal sealed class ZLinkLiveSocketConfig(
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendWeightedSocket socket,
    IZLinkSocketConfig recipe,
    Action<int>? weightChanged = null) : IZLinkSocketConfig
{
    // Weight set/get 는 admin 스레드 등 receive loop 와 다른 스레드에서 호출될 수 있다. native 옵션
    // 접근은 core 의 socket public-API lock(socket_public_api_lock_scope_t, setsockopt 경로)을 거치며,
    // 이는 framework 가 이미 caller 스레드에서 socket.Send 를 호출할 때 의존하는 것과 동일한 동시성
    // 경계다. 즉 weight mutation 은 기존 concurrent send 패턴보다 더 위험하지 않다.
    public int Weight
    {
        get => runtime.ExecuteOperation(socket.GetPeerWeight);
        set
        {
            ZLinkSocketConfig.ValidatePeerWeight(value);
            runtime.ExecuteOperation(() =>
            {
                socket.SetPeerWeight(value);
                recipe.Weight = value;
                weightChanged?.Invoke(value);
                return true;
            });
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
        return ZLinkRuntimeOptionsErrors.StartupOnly(name);
    }
}

internal static class ZLinkRuntimeOptionsErrors
{
    public static ZLinkConfigurationException StartupOnly(string name)
    {
        return new ZLinkConfigurationException(
            $"'{name}' is a startup-only option and cannot be changed at runtime; "
            + "set it at build time through the channel builder.");
    }
}

internal sealed class ZLinkFrozenSocketConfig(IZLinkSocketConfig recipe) : IZLinkSocketConfig
{
    public long MaxMessageSize { get => recipe.MaxMessageSize; set => throw StartupOnly(); }
    public int SendHighWaterMark { get => recipe.SendHighWaterMark; set => throw StartupOnly(); }
    public int ReceiveHighWaterMark { get => recipe.ReceiveHighWaterMark; set => throw StartupOnly(); }
    public int SendBufferSize { get => recipe.SendBufferSize; set => throw StartupOnly(); }
    public int ReceiveBufferSize { get => recipe.ReceiveBufferSize; set => throw StartupOnly(); }
    public TimeSpan? Linger { get => recipe.Linger; set => throw StartupOnly(); }
    public TimeSpan? ReceiveTimeout { get => recipe.ReceiveTimeout; set => throw StartupOnly(); }
    public TimeSpan? SendTimeout { get => recipe.SendTimeout; set => throw StartupOnly(); }
    public TimeSpan? ConnectTimeout { get => recipe.ConnectTimeout; set => throw StartupOnly(); }
    public TimeSpan? HandshakeInterval { get => recipe.HandshakeInterval; set => throw StartupOnly(); }
    public bool IPv6 { get => recipe.IPv6; set => throw StartupOnly(); }
    public bool TcpNoDelay { get => recipe.TcpNoDelay; set => throw StartupOnly(); }
    public bool Immediate { get => recipe.Immediate; set => throw StartupOnly(); }
    public int Weight { get => recipe.Weight; set => throw StartupOnly(); }

    private static ZLinkConfigurationException StartupOnly() =>
        ZLinkRuntimeOptionsErrors.StartupOnly("socket configuration");
}

internal sealed class ZLinkFrozenRouteConfig(IZLinkRouteConfig recipe) : IZLinkRouteConfig
{
    public bool RequireKnownPeer { get => recipe.RequireKnownPeer; set => throw StartupOnly(); }
    public bool AllowPeerHandover { get => recipe.AllowPeerHandover; set => throw StartupOnly(); }
    public bool EnablePeerProbe { get => recipe.EnablePeerProbe; set => throw StartupOnly(); }
    public RoutingId ConnectRoutingId { get => recipe.ConnectRoutingId; set => throw StartupOnly(); }

    private static ZLinkConfigurationException StartupOnly() =>
        ZLinkRuntimeOptionsErrors.StartupOnly("server routing configuration");
}

internal sealed class ZLinkFrozenOutboundRouteConfig(IZLinkOutboundRouteConfig recipe) : IZLinkOutboundRouteConfig
{
    public bool ProbeRouterOnConnect { get => recipe.ProbeRouterOnConnect; set => throw StartupOnly(); }

    private static ZLinkConfigurationException StartupOnly() =>
        ZLinkRuntimeOptionsErrors.StartupOnly("client routing configuration");
}

internal sealed class ZLinkClientServerRuntimeOptions(
    IZLinkSocketConfig? serverSocket,
    IZLinkRouteConfig? serverRouting,
    IZLinkSocketConfig? clientSocket,
    IZLinkOutboundRouteConfig? clientRouting)
    : IZLinkClientServerChannelOptions
{
    public IZLinkSocketConfig ConfigureServerSocket()
    {
        return serverSocket ?? throw MissingRole("server");
    }

    public IZLinkSocketConfig ConfigureClientSocket()
    {
        return clientSocket ?? throw MissingRole("client");
    }

    public IZLinkRouteConfig ConfigureServerRouting()
    {
        return serverRouting ?? throw MissingRole("server");
    }

    public IZLinkOutboundRouteConfig ConfigureClientRouting()
    {
        return clientRouting ?? throw MissingRole("client");
    }

    private static ZLinkConfigurationException MissingRole(string role) =>
        new($"Client-server channel has no {role} role on this node.");
}

internal sealed class ZLinkRouteMeshRuntimeOptions(IZLinkSocketConfig socket)
    : IZLinkRouteMeshChannelOptions
{
    public IZLinkSocketConfig ConfigureSocket()
    {
        return socket;
    }
}

internal sealed class ZLinkChannelRuntimeOptions(
    ZLinkFrameworkRuntime runtime,
    ZLinkLocationAutoConnectHost? autoConnect)
    : IZLinkChannelRuntimeOptions
{
    public IZLinkClientServerChannelOptions ClientServerChannel(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        return runtime.ResolveClientServerRuntimeOptions(channelName, autoConnect);
    }

    public IZLinkRouteMeshChannelOptions RouteMeshChannel(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        return new ZLinkRouteMeshRuntimeOptions(runtime.ResolveRouteMeshSocketConfig(channelName, autoConnect));
    }
}
