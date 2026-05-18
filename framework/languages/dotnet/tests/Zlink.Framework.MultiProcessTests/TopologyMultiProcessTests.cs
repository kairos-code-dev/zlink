using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Streams;
using Zlink.Framework.Tests.Common;

namespace Zlink.Framework.MultiProcessTests;

[CollectionDefinition(nameof(MultiProcessTestsCollection), DisableParallelization = true)]
public sealed class MultiProcessTestsCollection
{
}

[Collection(nameof(MultiProcessTestsCollection))]
public sealed class TopologyMultiProcessTests
{
    [Fact]
    public async Task RemoteRegistryQueryClient_Reads_FrameworkTopology_From_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var channelEndpoint = GetFreeTcpEndpoint();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var frameworkHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-server",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--server-endpoint", channelEndpoint);

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistryQueryClient(options =>
        {
            options.Endpoint = registryRouterEndpoint;
        });

        using var host = builder.Build();
        await host.StartAsync(timeout.Token);

        var client = host.Services.GetRequiredService<IZLinkRegistryQueryClient>();
        var snapshot = await RetryAsync(
            () => client.SnapshotAsync().AsTask(),
            entries => entries.Any(entry =>
                entry.ChannelName == "profile"
                && entry.Endpoint == channelEndpoint
                && entry.ServiceRole == ZLinkServiceRole.Router),
            TimeSpan.FromSeconds(15));

        Assert.Contains(snapshot, entry => entry.ChannelName == "profile");

        await host.StopAsync(timeout.Token);
    }

    [Fact]
    public async Task RegistryMonitoring_Emits_Topology_And_Summary_For_Remote_Framework_Process()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var channelEndpoint = GetFreeTcpEndpoint();

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<RegistryChangeProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkRegistryEvent>>(static provider =>
            provider.GetRequiredService<RegistryChangeProbe>());
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var frameworkHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-server",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--server-endpoint", channelEndpoint);

        var probe = host.Services.GetRequiredService<RegistryChangeProbe>();
        await probe.WaitForTopologyAsync(TimeSpan.FromSeconds(15));
        await probe.WaitForSummaryAsync(TimeSpan.FromSeconds(15));

        await host.StopAsync();
    }

    [Fact]
    public async Task SpotMonitoring_Emits_Peers_And_Subjects_For_Remote_Process_Node()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var localNodeEndpoint = GetFreeTcpEndpoint();
        var remoteNodeEndpoint = GetFreeTcpEndpoint();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotChangeProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkSpotEvent>>(static provider =>
            provider.GetRequiredService<SpotChangeProbe>());
        builder.Services.AddScoped<LocalMonitoringStageSubscriptionHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("local-stage-node", spot =>
            {
                spot.Bind(localNodeEndpoint);
                spot.EnablePubSub();
                spot.AddSpotFactory<LocalMonitoringStageSpot>("stage");
            });
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSpotEvents("local-stage-node", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync(timeout.Token);

        await using var remoteHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", "game.stage",
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "remote-stage-node",
            "--spot-bind-endpoint", remoteNodeEndpoint,
            "--enable-pubsub",
            "--spot-factory", "stage",
            "--create-spot", "stage");

        var probe = host.Services.GetRequiredService<SpotChangeProbe>();
        await probe.WaitForPeerAsync(TimeSpan.FromSeconds(15));
        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync("stage", timeout.Token);
        await probe.WaitForSubjectAsync(TimeSpan.FromSeconds(15));

        await host.StopAsync(timeout.Token);
    }

    [Fact]
    public async Task ChannelSubscriber_DiscoveryAttach_Receives_RemotePublish_From_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var publisherEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-channel-event-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var subscriberHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-subscriber",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--event-file", eventFilePath);
        await using var publisherHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-publisher",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--publisher-endpoint", publisherEndpoint,
            "--publish-topic", "profile.cache-invalidated",
            "--publish-value", "remote-channel");

        var line = await WaitForFileLineAsync(
            eventFilePath,
            static value => value.Contains("profile.cache-invalidated:remote-channel", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        Assert.Contains("profile.cache-invalidated:remote-channel", line, StringComparison.Ordinal);
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Publishes_Across_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-spot-event-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var subscriberHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", "game.stage",
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "subscriber-node",
            "--spot-bind-endpoint", subscriberNodeEndpoint,
            "--enable-pubsub",
            "--spot-factory", "stage",
            "--create-spot", "stage",
            "--event-file", eventFilePath);
        await using var publisherHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", "game.stage",
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "publisher-node",
            "--spot-bind-endpoint", publisherNodeEndpoint,
            "--enable-pubsub",
            "--attach-spot-publisher-channel", "game.stage",
            "--publish-topic", "stage.monitor",
            "--publish-value", "remote-spot");

        var line = await WaitForFileLineAsync(
            eventFilePath,
            static value => value.Contains("remote-spot", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        Assert.Contains("remote-spot", line, StringComparison.Ordinal);
    }

    [Fact]
    public async Task StreamRawSession_OnConnected_Emits_Metadata_Once_From_TestHostProcess()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-stream-connected-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var streamHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "stream-raw",
            "--stream-endpoint", streamEndpoint,
            "--event-file", eventFilePath);

        using var client = ConnectRawClient(streamEndpoint);
        var clientLocalPort = ((IPEndPoint)client.Client.LocalEndPoint!).Port;
        var network = client.GetStream();
        SendAll(network, BuildStreamPacketFrame(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Request,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRequestSeq,
                new ZlinkStreamRequestSeq(1),
                "ping",
                ZlinkStreamMetadata.Empty),
            "\"ping\""u8));
        var reply = ReceiveFrame(network);
        Assert.Equal(ZlinkStreamMessageKind.Response, reply.Header.Kind);
        Assert.Equal("ping", reply.Header.Name);
        Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Payload));

        var connectedLine = await WaitForFileLineAsync(
            eventFilePath,
            static value => value.StartsWith("connected|", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        var parts = connectedLine.Split('|');
        Assert.Equal(5, parts.Length);
        Assert.False(string.IsNullOrWhiteSpace(parts[1]));
        Assert.False(string.IsNullOrWhiteSpace(parts[2]));
        Assert.StartsWith("tcp://", parts[3], StringComparison.Ordinal);
        Assert.StartsWith("tcp://", parts[4], StringComparison.Ordinal);
        Assert.Contains($":{new Uri(streamEndpoint).Port}", parts[3], StringComparison.Ordinal);
        Assert.Contains($":{clientLocalPort}", parts[4], StringComparison.Ordinal);

        var connectedCount = (await File.ReadAllLinesAsync(eventFilePath))
            .Count(static line => line.StartsWith("connected|", StringComparison.Ordinal));
        Assert.Equal(1, connectedCount);
    }

    [Fact]
    public async Task StreamRawSession_OnError_Reports_TransportError_For_RemoteDisconnect()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var eventFilePath = Path.Combine(
            Path.GetTempPath(),
            $"zlink-framework-stream-error-{Guid.NewGuid():N}.log");

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var streamHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "stream-raw",
            "--stream-endpoint", streamEndpoint,
            "--event-file", eventFilePath);

        var client = ConnectRawClient(streamEndpoint);
        var network = client.GetStream();
        SendAll(network, BuildStreamPacketFrame(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Request,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.HasRequestSeq,
                new ZlinkStreamRequestSeq(1),
                "ping",
                ZlinkStreamMetadata.Empty),
            "\"ping\""u8));
        _ = ReceiveFrame(network);
        client.Dispose();

        var errorLine = await WaitForFileLineAsync(
            eventFilePath,
            static value => string.Equals(value, "error|TransportError", StringComparison.Ordinal),
            TimeSpan.FromSeconds(15));

        Assert.Equal("error|TransportError", errorLine);
    }

    private static string GetFreeTcpEndpoint()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }

    private static TcpClient ConnectRawClient(string endpoint)
    {
        var uri = new Uri(endpoint);
        var client = new TcpClient();
        client.NoDelay = true;
        client.ReceiveTimeout = 5000;
        client.SendTimeout = 5000;
        client.Connect(IPAddress.Parse(uri.Host), uri.Port);
        return client;
    }

    private static void SendAll(NetworkStream stream, ReadOnlySpan<byte> payload)
    {
        stream.Write(payload);
        stream.Flush();
    }

    private static byte[] ReceiveExact(NetworkStream stream, int size)
    {
        var buffer = new byte[size];
        var read = 0;
        while (read < size)
        {
            var current = stream.Read(buffer, read, size - read);
            if (current <= 0)
            {
                throw new TimeoutException("STREAM receive timeout");
            }

            read += current;
        }

        return buffer;
    }

    private static byte[] BuildStreamPacketFrame(
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload)
    {
        var headerBytes = ZLinkStreamProtocolDefaults.HeaderCodec.Encode(header).ToArray();
        var frame = new byte[6 + headerBytes.Length + payload.Length];
        frame[0] = (byte)(headerBytes.Length >> 8);
        frame[1] = (byte)headerBytes.Length;
        frame[2] = (byte)(payload.Length >> 24);
        frame[3] = (byte)(payload.Length >> 16);
        frame[4] = (byte)(payload.Length >> 8);
        frame[5] = (byte)payload.Length;
        headerBytes.CopyTo(frame.AsSpan(6, headerBytes.Length));
        payload.CopyTo(frame.AsSpan(6 + headerBytes.Length, payload.Length));
        return frame;
    }

    private static (ZlinkStreamHeader Header, byte[] Payload) ReceiveFrame(NetworkStream stream)
    {
        var lengths = ReceiveExact(stream, 6);
        var headerLength = (lengths[0] << 8) | lengths[1];
        var payloadLength = (lengths[2] << 24) | (lengths[3] << 16) | (lengths[4] << 8) | lengths[5];
        var headerBytes = ReceiveExact(stream, headerLength);
        var payloadBytes = ReceiveExact(stream, payloadLength);
        var header = ZLinkStreamProtocolDefaults.HeaderCodec.Decode(headerBytes);
        return (header, payloadBytes);
    }

    private static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("Multi-process retry timed out.");
    }

    private static async Task<string> WaitForFileLineAsync(
        string path,
        Func<string, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;

        while (DateTime.UtcNow < deadline)
        {
            if (File.Exists(path))
            {
                var lines = await File.ReadAllLinesAsync(path);
                var match = lines.FirstOrDefault(predicate);
                if (match is not null)
                {
                    return match;
                }
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        throw new TimeoutException($"Timed out waiting for event file '{path}'.");
    }

    public sealed class RegistryChangeProbe : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
    {
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _topology =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _summary =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkRegistryEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (@event.Event == ZLinkRegistryEventKind.TopologyChanged
                && @event.Topology is { Count: > 0 })
            {
                _topology.TrySetResult(@event);
            }

            if (@event.Event == ZLinkRegistryEventKind.ServiceSummaryChanged
                && @event.ServiceSummary is { Count: > 0 })
            {
                _summary.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkRegistryEvent> WaitForTopologyAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _topology.Task.WaitAsync(timeoutSource.Token);
        }

        public async Task<ZLinkRegistryEvent> WaitForSummaryAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _summary.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class SpotChangeProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _peer =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<ZLinkSpotEvent> _subject =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (@event.Event == ZLinkSpotEventKind.PeersChanged
                && @event.Peers is { Count: > 0 })
            {
                _peer.TrySetResult(@event);
            }

            if (@event.Event == ZLinkSpotEventKind.SubjectsChanged
                && @event.Subjects is { Count: > 0 })
            {
                _subject.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitForPeerAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _peer.Task.WaitAsync(timeoutSource.Token);
        }

        public async Task<ZLinkSpotEvent> WaitForSubjectAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _subject.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class LocalMonitoringStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<LocalMonitoringStageSubscriptionHandler>("stage.monitor");
        }
    }

    public sealed record LocalMonitoringStageEvent(string Value);

    public sealed class LocalMonitoringStageSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<LocalMonitoringStageSpot, LocalMonitoringStageEvent>
    {
        public ValueTask HandleAsync(
            LocalMonitoringStageSpot spot,
            LocalMonitoringStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = message;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }
}
