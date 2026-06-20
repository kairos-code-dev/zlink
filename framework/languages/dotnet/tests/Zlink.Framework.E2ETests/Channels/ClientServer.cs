using System.Collections.Concurrent;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
[Collection(nameof(FrameworkTestsCollection))]
public sealed class ClientServerTests
{
    [Fact(DisplayName = "CH-001 discovery client-server request-response across hosts")]
    public async Task DiscoveryClient_Request_And_Send_Work_Across_Hosts()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ClientServerTests>();
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableServer(apiEndpoint);
                channel.AddHandlerGroup("profile");

            }
        });
        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableClient();

            }
        });

        using var registryHost = registryBuilder.Build();
        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await registryHost.StartAsync();
        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.RequestToChannel("api", new GetProfileRequest { UserId = "discovery" }).Async<ProfileReply>(),
            static result => result.Name == "user:discovery");

        Assert.Equal("user:discovery", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await client.SendToChannel("api", new RefreshProfileCacheCommand { UserId = "discovery" }).Async();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("discovery", recorder.Commands.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost, registryHost);
    }

    [Fact(DisplayName = "CH-001 manual client-server request-response across hosts")]
    public async Task ManualClient_Request_And_Send_Work_Across_Hosts()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ClientServerTests>();
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableServer(apiEndpoint);
                channel.AddHandlerGroup("profile");

            }
        });
        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableClient(apiEndpoint);

            }
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.RequestToChannel("api", new GetProfileRequest { UserId = "alice" }).Async<ProfileReply>(),
            static result => result.Name == "user:alice");

        Assert.Equal("user:alice", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await client.SendToChannel("api", new RefreshProfileCacheCommand { UserId = "alice" }).Async();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Commands.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }

    [Fact(DisplayName = "CH-006 manual client-server send one-way records server evidence")]
    public async Task ManualClient_Send_OneWay_Records_Server_Evidence()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ClientServerTests>();
            var channel = options.AddClientServerChannel("api");
            channel.EnableServer(apiEndpoint);
            channel.AddHandlerGroup("profile");
        });
        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("api");
            channel.EnableClient(apiEndpoint);
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        await client.SendToChannel("api", new RefreshProfileCacheCommand { UserId = "send-only" }).Async();

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await Task.Yield();
                return recorder.Commands.ToArray();
            },
            commands => commands.Contains("send-only"));

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }

    [Fact(DisplayName = "CH-002 manual endpoint round-robin distributes requests across three servers")]
    public async Task ManualClient_RoundRobin_Distributes_Requests_Across_Three_Servers()
    {
        var endpoints = Enumerable.Range(0, 3)
            .Select(_ => $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}")
            .ToArray();
        var states = new[]
        {
            new RoundRobinServerEvidence("server-a"),
            new RoundRobinServerEvidence("server-b"),
            new RoundRobinServerEvidence("server-c"),
        };
        using var serverA = BuildRoundRobinServer(endpoints[0], states[0]);
        using var serverB = BuildRoundRobinServer(endpoints[1], states[1]);
        using var serverC = BuildRoundRobinServer(endpoints[2], states[2]);
        using var clientHost = BuildRoundRobinClient(endpoints);

        await serverA.StartAsync();
        await serverB.StartAsync();
        await serverC.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var seenServers = new HashSet<string>(StringComparer.Ordinal);
        for (var i = 0; i < 30 && seenServers.Count < 3; i++)
        {
            var warmupIndex = i;
            var warmup = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("round-robin", new RoundRobinProbeRequest { RequestId = $"warmup-{warmupIndex}" })
                    .PacketName("RoundRobinProbe")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<RoundRobinProbeReply>(),
                reply => reply.RequestId == $"warmup-{warmupIndex}");
            seenServers.Add(warmup.ServerId);
        }

        Assert.Equal(new[] { "server-a", "server-b", "server-c" }, seenServers.Order(StringComparer.Ordinal));

        var replies = new List<RoundRobinProbeReply>();
        for (var i = 0; i < 90; i++)
        {
            replies.Add(await client
                .RequestToChannel("round-robin", new RoundRobinProbeRequest { RequestId = $"verify-{i}" })
                .PacketName("RoundRobinProbe")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<RoundRobinProbeReply>());
        }

        AssertRoundRobinCounts(
            replies.GroupBy(reply => reply.ServerId).ToDictionary(group => group.Key, group => group.Count()));
        AssertRoundRobinCounts(states.ToDictionary(
            state => state.ServerId,
            state => state.RequestIds.Count(requestId => requestId.StartsWith("verify-", StringComparison.Ordinal))));

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverC, serverB, serverA);
    }

    [Fact(DisplayName = "CH-007 manual client-server request timeout keeps client usable after late reply")]
    public async Task ManualClient_Request_Timeout_Keeps_Client_Usable_After_Late_Reply()
    {
        var endpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var evidence = new TimeoutServerEvidence();
        using var serverHost = BuildTimeoutServer(endpoint, evidence);
        using var clientHost = BuildTimeoutClient(endpoint);

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        try
        {
            var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();

            var warmup = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .RequestToChannel("timeout", new TimeoutProbeRequest
                    {
                        RequestId = "warmup",
                        DelayMilliseconds = 0,
                    })
                    .PacketName("TimeoutProbe")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<TimeoutProbeReply>(),
                reply => reply.RequestId == "warmup");
            Assert.Equal("completed:warmup", warmup.Status);

            await Assert.ThrowsAsync<TimeoutException>(async () => await client
                .RequestToChannel("timeout", new TimeoutProbeRequest
                {
                    RequestId = "late-1",
                    DelayMilliseconds = 300,
                })
                .PacketName("TimeoutProbe")
                .Timeout(TimeSpan.FromMilliseconds(50))
                .Async<TimeoutProbeReply>());

            await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () =>
                {
                    await Task.Yield();
                    return evidence.Completed.ToArray();
                },
                completed => completed.Contains("late-1"));

            var afterLateReply = await client
                .RequestToChannel("timeout", new TimeoutProbeRequest
                {
                    RequestId = "after-late-reply",
                    DelayMilliseconds = 0,
                })
                .PacketName("TimeoutProbe")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<TimeoutProbeReply>();

            Assert.Equal("after-late-reply", afterLateReply.RequestId);
            Assert.Equal("completed:after-late-reply", afterLateReply.Status);
            Assert.Contains("late-1", evidence.Started.ToArray());
            Assert.Contains("after-late-reply", evidence.Completed.ToArray());
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
        }
    }

    private static IHost BuildRoundRobinServer(string endpoint, RoundRobinServerEvidence evidence)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(evidence);
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("round-robin");
            channel.EnableServer(endpoint);
            channel.AddRequestHandler<RoundRobinProbeHandler, RoundRobinProbeRequest, RoundRobinProbeReply>("RoundRobinProbe");
        });
        return builder.Build();
    }

    private static IHost BuildTimeoutServer(string endpoint, TimeoutServerEvidence evidence)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(evidence);
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("timeout");
            channel.EnableServer(endpoint);
            channel.AddRequestHandler<TimeoutProbeHandler, TimeoutProbeRequest, TimeoutProbeReply>("TimeoutProbe");
        });
        return builder.Build();
    }

    private static IHost BuildTimeoutClient(string endpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("timeout");
            channel.EnableClient(endpoint);
        });
        return builder.Build();
    }

    private static IHost BuildRoundRobinClient(IReadOnlyList<string> endpoints)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            var channel = options.AddClientServerChannel("round-robin");
            foreach (var endpoint in endpoints)
            {
                channel.EnableClient(endpoint);
            }
        });
        return builder.Build();
    }

    private static void AssertRoundRobinCounts(IReadOnlyDictionary<string, int> counts)
    {
        Assert.Equal(3, counts.Count);
        Assert.Equal(30, counts["server-a"]);
        Assert.Equal(30, counts["server-b"]);
        Assert.Equal(30, counts["server-c"]);
    }
}

public sealed class TimeoutProbeRequest
{
    public string RequestId { get; init; } = string.Empty;

    public int DelayMilliseconds { get; init; }
}

public sealed class TimeoutProbeReply
{
    public string RequestId { get; init; } = string.Empty;

    public string Status { get; init; } = string.Empty;
}

public sealed class TimeoutServerEvidence
{
    public ConcurrentQueue<string> Started { get; } = [];

    public ConcurrentQueue<string> Completed { get; } = [];
}

public sealed class TimeoutProbeHandler(TimeoutServerEvidence evidence)
    : IZLinkRequestHandler<TimeoutProbeRequest, TimeoutProbeReply>
{
    public async ValueTask<TimeoutProbeReply> HandleAsync(
        TimeoutProbeRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        evidence.Started.Enqueue(request.RequestId);
        if (request.DelayMilliseconds > 0)
        {
            await Task.Delay(TimeSpan.FromMilliseconds(request.DelayMilliseconds), cancellationToken);
        }

        evidence.Completed.Enqueue(request.RequestId);
        return new TimeoutProbeReply
        {
            RequestId = request.RequestId,
            Status = $"completed:{request.RequestId}",
        };
    }
}

public sealed class RoundRobinProbeRequest
{
    public string RequestId { get; init; } = string.Empty;
}

public sealed class RoundRobinProbeReply
{
    public string RequestId { get; init; } = string.Empty;

    public string ServerId { get; init; } = string.Empty;
}

public sealed class RoundRobinServerEvidence(string serverId)
{
    public string ServerId { get; } = serverId;

    public ConcurrentQueue<string> RequestIds { get; } = [];
}

public sealed class RoundRobinProbeHandler(RoundRobinServerEvidence evidence)
    : IZLinkRequestHandler<RoundRobinProbeRequest, RoundRobinProbeReply>
{
    public ValueTask<RoundRobinProbeReply> HandleAsync(
        RoundRobinProbeRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        evidence.RequestIds.Enqueue(request.RequestId);
        return ValueTask.FromResult(new RoundRobinProbeReply
        {
            RequestId = request.RequestId,
            ServerId = evidence.ServerId,
        });
    }
}
