using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;

[Collection(nameof(FrameworkTestsCollection))]
public sealed class ChannelRuntimeOptionsTests
{
    [Fact(DisplayName = "DRAIN client-server: build-time weight, runtime drain/restore, range + startup-only errors")]
    public async Task ClientServer_Weight_Build_And_Runtime()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("api");
            channel.EnableServer(apiEndpoint);
            channel.AddRequestHandler<DrainPingHandler, DrainPing, DrainPong>();
            channel.ConfigureServerSocket().Weight = 30; // build-time 초기 weight (DRAIN-016)
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<IZLinkChannelRuntimeOptions>();
        var serverSocket = runtime.ClientServerChannel("api").ConfigureServerSocket();

        // build-time 초기값이 live socket 에 반영된다.
        Assert.Equal(30, serverSocket.Weight);

        // runtime drain → restore (DRAIN-003/012)
        serverSocket.Weight = 0;
        Assert.Equal(0, serverSocket.Weight);
        serverSocket.Weight = 100;
        Assert.Equal(100, serverSocket.Weight);

        // 범위 밖 weight 오류 (DRAIN-010)
        Assert.Throws<ZLinkConfigurationException>(() => serverSocket.Weight = 101);
        Assert.Throws<ZLinkConfigurationException>(() => serverSocket.Weight = -1);

        // startup 전용 속성 런타임 set 거부 (DRAIN-013), read 는 가능
        _ = serverSocket.IPv6;
        Assert.Throws<ZLinkConfigurationException>(() => serverSocket.IPv6 = true);
        Assert.Throws<ZLinkConfigurationException>(() => serverSocket.MaxMessageSize = 1024);

        await host.StopAsync();
    }

    [Fact(DisplayName = "DRAIN errors: unknown channel and wrong-kind getter")]
    public async Task RuntimeOptions_Error_Policy()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("api");
            channel.EnableServer(apiEndpoint);
            channel.AddRequestHandler<DrainPingHandler, DrainPing, DrainPong>();
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<IZLinkChannelRuntimeOptions>();

        // 미지 channel (DRAIN-007)
        Assert.Throws<ZLinkConfigurationException>(() => runtime.ClientServerChannel("nope"));

        // 종류 불일치 getter: client-server 채널을 dealer mesh 접근자로 (DRAIN-008)
        Assert.Throws<ZLinkConfigurationException>(() => runtime.DealerMeshChannel("api"));

        await host.StopAsync();
    }

    [Fact(DisplayName = "DRAIN dealer mesh: build-time weight + runtime drain/restore on shared DEALER")]
    public async Task DealerMesh_Weight_Build_And_Runtime()
    {
        var meshEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var peerEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddDealerMeshChannel("mesh");
            channel.EnableServer(meshEndpoint);
            channel.EnableClient(peerEndpoint); // 메시 노드는 peer source 필요(lazy connect)
            channel.AddRequestHandler<DrainPingHandler, DrainPing, DrainPong>();
            channel.ConfigureSocket().Weight = 40; // build-time 초기 weight
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<IZLinkChannelRuntimeOptions>();
        var socket = runtime.DealerMeshChannel("mesh").ConfigureSocket();

        Assert.Equal(40, socket.Weight); // build-time 초기값 (DEALER 캐시)
        socket.Weight = 0;               // drain (DRAIN-004 경로)
        Assert.Equal(0, socket.Weight);
        socket.Weight = 100;
        Assert.Equal(100, socket.Weight);
        Assert.Throws<ZLinkConfigurationException>(() => socket.Weight = 200);

        // 종류 불일치: dealer mesh 채널을 client-server 접근자로
        Assert.Throws<ZLinkConfigurationException>(() => runtime.ClientServerChannel("mesh"));

        await host.StopAsync();
    }

    [Fact(DisplayName = "DRAIN route mesh: build-time weight + runtime drain/restore on serving ROUTER")]
    public async Task RouteMesh_Weight_Build_And_Runtime()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var peerEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var routed = options.AddRouteMeshChannel("backend");
            routed.EnableServer(endpoint);
            routed.SetRoutingId(RoutingId.From("77"));
            routed.EnableClient(peerEndpoint); // route mesh 노드는 peer source 필요(lazy connect)
            routed.AddRequestHandler<RouteDrainHandler, DrainPing, DrainPong>();
            routed.ConfigureSocket().Weight = 55; // build-time 초기 weight
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<IZLinkChannelRuntimeOptions>();
        var socket = runtime.RouteMeshChannel("backend").ConfigureSocket();

        Assert.Equal(55, socket.Weight); // build-time 초기값 (ROUTER live get)
        socket.Weight = 0;               // drain (DRAIN-005 경로)
        Assert.Equal(0, socket.Weight);
        socket.Weight = 100;
        Assert.Equal(100, socket.Weight);
        Assert.Throws<ZLinkConfigurationException>(() => socket.Weight = -5);

        // 미지 route mesh channel
        Assert.Throws<ZLinkConfigurationException>(() => runtime.RouteMeshChannel("nope"));

        await host.StopAsync();
    }

    public sealed record DrainPing(string Value);

    public sealed record DrainPong(string Value);

    public sealed class DrainPingHandler : IZLinkRequestHandler<DrainPing, DrainPong>
    {
        public ValueTask<DrainPong> HandleAsync(
            DrainPing request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new DrainPong(request.Value));
        }
    }

    public sealed class RouteDrainHandler : IZLinkRouteRequestHandler<DrainPing, DrainPong>
    {
        public ValueTask<DrainPong> HandleAsync(
            DrainPing request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new DrainPong(request.Value));
        }
    }
}
