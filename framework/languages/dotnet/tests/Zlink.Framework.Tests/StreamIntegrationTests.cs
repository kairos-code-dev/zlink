using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

[CollectionDefinition(nameof(StreamIntegrationTestsCollection), DisableParallelization = true)]
public sealed class StreamIntegrationTestsCollection
{
}

[Collection(nameof(StreamIntegrationTestsCollection))]
public sealed class StreamIntegrationTests
{
    private static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);

    [Fact]
    public void StreamSessionRuntime_Only_Exposes_Enqueue_Callback_Entrypoints()
    {
        var runtimeType = typeof(ZLinkStreamSessionRuntime);

        Assert.Null(runtimeType.GetMethod("MarkConnectedAsync"));
        Assert.Null(runtimeType.GetMethod("DispatchPacketAsync"));
        Assert.Null(runtimeType.GetMethod("MarkDisconnectedAsync"));
        Assert.NotNull(runtimeType.GetMethod("EnqueueConnected"));
        Assert.NotNull(runtimeType.GetMethod("EnqueuePacket"));
        Assert.NotNull(runtimeType.GetMethod("EnqueueDisconnected"));
    }

    [Fact]
    public void SessionActorDispatch_Uses_Multipart_Routed_Actor_Dispatch()
    {
        var headerCodec = ZlinkStreamDefaultCodecs.Header();
        var routeHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            "gateway",
            ZLinkInternalPacketNames.ActorDispatch,
            ZLinkEnvelopeCodec.DefaultContentType,
            "correlation",
            null,
            null,
            null,
            null);
        var streamHeader = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasMetadata,
            null,
            "relay.echo",
            ZlinkStreamMetadata.Empty.With("trace-id", "trace-1"));
        var payloadParts = new[]
        {
            ZLinkEnvelopeCodec.EncodePart(new ZLinkActorDispatchMetadata("player-1", "player")),
            Message.FromBytes(headerCodec.Encode(streamHeader).Span),
            Message.FromString("{\"value\":\"ping\"}")
        };
        var parts = new Message[payloadParts.Length + 1];
        parts[0] = ZLinkEnvelopeCodec.EncodeHeader(routeHeader);
        Array.Copy(payloadParts, 0, parts, 1, payloadParts.Length);

        try
        {
            Assert.Equal(4, parts.Length);
            Assert.Equal(routeHeader, ZLinkEnvelopeCodec.DecodeHeader(parts));
            Assert.Equal(new ZLinkActorDispatchMetadata("player-1", "player"),
                ZLinkEnvelopeCodec.DecodePart<ZLinkActorDispatchMetadata>(parts[1]));
            Assert.Equal("relay.echo", headerCodec.Decode(parts[2].AsReadOnlyMemory()).Name);
            Assert.Equal("{\"value\":\"ping\"}", Encoding.UTF8.GetString(parts[3].AsReadOnlySpan()));
            Assert.Null(typeof(ZLinkEnvelopeCodec).GetMethod(
                "Encode",
                BindingFlags.Public | BindingFlags.Static));
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public void SessionProxy_Uses_Multipart_Routed_Client_Push()
    {
        var routeHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "gateway",
            ZLinkInternalPacketNames.SessionProxy,
            ZLinkEnvelopeCodec.DefaultContentType,
            "correlation",
            DateTimeOffset.UtcNow.AddSeconds(5),
            null,
            null,
            null);
        var parts = new[]
        {
            ZLinkEnvelopeCodec.EncodeHeader(routeHeader),
            ZLinkEnvelopeCodec.EncodePart(new ZLinkSessionProxyEnvelope(
                "player-1",
                "binding-token",
                "client.echo",
                true,
                new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    ["trace-id"] = "trace-1"
                })),
            Message.FromString("{\"value\":\"ping\"}")
        };

        try
        {
            Assert.Equal(routeHeader, ZLinkEnvelopeCodec.DecodeHeader(parts));
            var envelope = ZLinkEnvelopeCodec.DecodePart<ZLinkSessionProxyEnvelope>(parts[1]);
            Assert.Equal("player-1", envelope.ActorId);
            Assert.Equal("client.echo", envelope.PacketName);
            Assert.True(envelope.ExpectsReply);
            Assert.Equal("{\"value\":\"ping\"}", Encoding.UTF8.GetString(parts[2].AsReadOnlySpan()));
            Assert.Null(typeof(ZLinkEnvelopeCodec).GetMethod(
                "Encode",
                BindingFlags.Public | BindingFlags.Static));
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var clientLocalPort = ((IPEndPoint)client.Client.LocalEndPoint!).Port;
            var network = client.GetStream();
            try
            {
                await RetryAsync(
                    async () =>
                    {
                        SendAll(network, BuildStreamPacketFrame(
                            new ZlinkStreamHeader(
                                ZlinkStreamMessageKind.Request,
                                ZlinkStreamCodec.Json,
                                ZlinkStreamHeaderFlags.HasRequestSeq,
                                new ZlinkStreamRequestSeq(1),
                                "ping",
                                ZlinkStreamMetadata.Empty),
                            "\"ping\""u8));
                        await Task.Yield();
                        callbackCapture.ThrowIfAny();
                        return recorder.ReceivedPayloads.Contains("ping");
                    },
                    received => received,
                    TimeSpan.FromSeconds(5));
            }
            catch (Exception ex) when (ex is TimeoutException or AggregateException)
            {
                throw new TimeoutException(
                    $"STREAM raw retry timed out. Connected={recorder.ConnectedCount}, Disconnected={recorder.DisconnectedCount}, Errors={recorder.ErrorCount}, Received={string.Join(',', recorder.ReceivedPayloads)}",
                    ex);
            }

            await RetryAsync(
                () => recorder.LastSessionId is not null
                    && recorder.LastRoutingId is not null
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            var reply = ReceiveFrame(network, new ZlinkStreamRequestSeq(1));
            Assert.Equal(ZlinkStreamMessageKind.Response, reply.Header.Kind);
            Assert.Equal(new ZlinkStreamRequestSeq(1), reply.Header.RequestSeq);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Body));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(2),
                    "ping",
                    ZlinkStreamMetadata.Empty),
                "\"ping-2\""u8));
            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("ping-2") && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            var secondReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(2));
            Assert.Equal(ZlinkStreamMessageKind.Response, secondReply.Header.Kind);
            Assert.Equal(new ZlinkStreamRequestSeq(2), secondReply.Header.RequestSeq);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(secondReply.Body));

            Assert.NotNull(recorder.LastSessionId);
            Assert.NotNull(recorder.LastRoutingId);
            AssertStreamMetadata(endpoint, clientLocalPort, recorder.LastLocalAddr, recorder.LastRemoteAddr);
            Assert.True(
                recorder.ConnectedCount == 1,
                $"Expected one connected callback, got {recorder.ConnectedCount}. Connected sessions={string.Join(',', recorder.ConnectedSessionIds)}.");

            client.Dispose();
            await RetryAsync(
                () => recorder.DisconnectedCount > 0 && recorder.ErrorCount > 0,
                TimeSpan.FromSeconds(5));
            Assert.Equal(ZLinkStreamSessionError.TransportError, recorder.LastError?.Error);
            Assert.NotNull(recorder.LastSessionId);
            Assert.Equal(1, recorder.MaxConcurrentCallbacksFor(recorder.LastSessionId));
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Can_Close_Current_Client_Stream()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(1),
                    "close",
                    ZlinkStreamMetadata.Empty),
                "\"close\""u8));

            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("close")
                    && recorder.DisconnectedCount > 0
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            Assert.Equal(0, recorder.ErrorCount);
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString("0101");
        var playRid = RoutingId.FromString("0202");
        var proxyRecorder = new ActorDispatchRecorder();
        var sessionLocations = new ActorSessionLocationStore();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(proxyRecorder);
            services.AddSingleton(sessionLocations);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddZLinkFramework(options =>
            {
                options.ConfigureMetadata(metadata =>
                {
                    metadata.ForwardApplicationKey("trace-id");
                });
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddActorSessionBindingStore<ActorSessionLocationStore>();
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(playRouterEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = playRid);
                    routed.UseManualConnections(connections => connections.Connect(sessionRouterEndpoint));
                });
            });
        });

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new ActorPlayRouteStore(playRid));
            services.AddSingleton(sessionLocations);
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorSessionBindingStore<ActorSessionLocationStore>();
                options.AddActorPlayRouteResolver<ActorPlayRouteStore>();
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(sessionRouterEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = sessionRid);
                    routed.UseManualConnections(connections => connections.Connect(playRouterEndpoint));
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AddHeaderSession<GatewayRelaySession>();
                });
            });
        });

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();
            await Task.Delay(250);

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(101),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty
                        .With("trace-id", "trace-101")
                        .With("tenant-id", "tenant-denied")),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("from-client"), JsonOptions)));

            var relayReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(101));
            var relayBody = JsonSerializer.Deserialize<GatewayPong>(relayReply.Body, JsonOptions);
            Assert.Equal(ZlinkStreamMessageKind.Response, relayReply.Header.Kind);
            Assert.Equal("play:from-client", relayBody?.Value);
            Assert.Equal(101UL, relayBody?.RequestSeq);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-101", proxyRecorder.LastTraceId);
            Assert.False(proxyRecorder.ForwardedTenantId);

            var sessionProxy = playHost.Services.GetRequiredService<IZLinkSessionProxy>();
            var clientReplyTask = Task.Run(() =>
            {
                var request = ReceiveFrame(network);
                Assert.Equal(ZlinkStreamMessageKind.Request, request.Header.Kind);
                Assert.Equal("client.echo", request.Header.Name);
                Assert.NotNull(request.Header.RequestSeq);
                var ping = JsonSerializer.Deserialize<GatewayPing>(request.Body, JsonOptions);
                Assert.Equal("from-play", ping?.Value);
                SendAll(network, BuildStreamPacketFrame(
                    new ZlinkStreamHeader(
                        ZlinkStreamMessageKind.Response,
                        ZlinkStreamCodec.Json,
                        ZlinkStreamHeaderFlags.HasRequestSeq,
                        request.Header.RequestSeq,
                        request.Header.Name,
                        ZlinkStreamMetadata.Empty),
                    JsonSerializer.SerializeToUtf8Bytes(
                        new GatewayPong("client:from-play", request.Header.RequestSeq!.Value.Value),
                        JsonOptions)));
            });
            var gatewayReply = await sessionProxy.Request(
                    "player-1",
                    new GatewayPing("from-play"))
                .PacketName("client.echo")
                .Timeout(TimeSpan.FromSeconds(10))
                .SubmitAsync<GatewayPong>();

            await clientReplyTask;
            Assert.Equal("client:from-play", gatewayReply.Value);
            Assert.NotEqual(0UL, gatewayReply.RequestSeq);

            var actorClient = sessionHost.Services.GetRequiredService<IZLinkActorClient>();
            var actorClientReply = await actorClient.Request(
                    "player-1",
                    new GatewayPing("from-actor-client"))
                .PacketName("relay.echo")
                .Metadata("trace-id", "trace-actor-client")
                .Timeout(TimeSpan.FromSeconds(10))
                .SubmitAsync<GatewayPong>();

            Assert.Equal("play:from-actor-client", actorClientReply.Value);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-actor-client", proxyRecorder.LastTraceId);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }

    private static async Task<IHost> CreateHostAsync(
        string endpoint,
        Action<IServiceCollection> configure)
    {
        _ = endpoint;
        var builder = Host.CreateApplicationBuilder();
        configure(builder.Services);
        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(PollingInterval);
        }

        throw new TimeoutException("STREAM integration retry timed out.");
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

            await Task.Delay(PollingInterval);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("STREAM integration retry timed out.");
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
        ReadOnlySpan<byte> body)
    {
        var headerBytes = ZlinkStreamDefaultCodecs.Header().Encode(header).ToArray();
        var frame = new byte[6 + headerBytes.Length + body.Length];
        frame[0] = (byte)(headerBytes.Length >> 8);
        frame[1] = (byte)headerBytes.Length;
        frame[2] = (byte)(body.Length >> 24);
        frame[3] = (byte)(body.Length >> 16);
        frame[4] = (byte)(body.Length >> 8);
        frame[5] = (byte)body.Length;

        headerBytes.CopyTo(frame.AsSpan(6, headerBytes.Length));
        body.CopyTo(frame.AsSpan(6 + headerBytes.Length, body.Length));
        return frame;
    }

    private static (ZlinkStreamHeader Header, byte[] Body) ReceiveFrame(NetworkStream stream)
    {
        var lengths = ReceiveExact(stream, 6);
        var headerLength = (lengths[0] << 8) | lengths[1];
        var bodyLength = (lengths[2] << 24) | (lengths[3] << 16) | (lengths[4] << 8) | lengths[5];
        var headerBytes = ReceiveExact(stream, headerLength);
        var bodyBytes = ReceiveExact(stream, bodyLength);
        var header = ZlinkStreamDefaultCodecs.Header().Decode(headerBytes);
        return (header, bodyBytes);
    }

    private static (ZlinkStreamHeader Header, byte[] Body) ReceiveFrame(
        NetworkStream stream,
        ZlinkStreamRequestSeq requestSeq)
    {
        while (true)
        {
            var frame = ReceiveFrame(stream);
            if (frame.Header.RequestSeq == requestSeq)
            {
                return frame;
            }
        }
    }

    private static void AssertStreamMetadata(
        string endpoint,
        int clientLocalPort,
        string? localAddr,
        string? remoteAddr)
    {
        Assert.False(string.IsNullOrWhiteSpace(localAddr));
        Assert.False(string.IsNullOrWhiteSpace(remoteAddr));

        var serverPort = new Uri(endpoint).Port;
        Assert.StartsWith("tcp://", localAddr, StringComparison.Ordinal);
        Assert.StartsWith("tcp://", remoteAddr, StringComparison.Ordinal);
        Assert.Contains($":{serverPort}", localAddr!, StringComparison.Ordinal);
        Assert.Contains($":{clientLocalPort}", remoteAddr!, StringComparison.Ordinal);
    }

    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public sealed class HeaderStreamRecorder
    {
        public ConcurrentBag<string> ReceivedPayloads { get; } = [];

        public string? LastSessionId { get; set; }

        public global::Systems.Zlink.RoutingId? LastRoutingId { get; set; }

        public string? LastLocalAddr { get; set; }

        public string? LastRemoteAddr { get; set; }

        public int ConnectedCount { get; set; }

        public ConcurrentBag<string> ConnectedSessionIds { get; } = [];

        public int DisconnectedCount { get; set; }

        public int ErrorCount { get; set; }

        public ZLinkStreamError? LastError { get; set; }

        private readonly ConcurrentDictionary<string, CallbackConcurrency> _callbackConcurrencyBySession = new();

        public IDisposable EnterCallback(string sessionId)
        {
            var concurrency = _callbackConcurrencyBySession.GetOrAdd(sessionId, _ => new CallbackConcurrency());
            concurrency.Enter();
            return new CallbackLease(concurrency);
        }

        public int MaxConcurrentCallbacksFor(string sessionId)
        {
            return _callbackConcurrencyBySession.TryGetValue(sessionId, out var concurrency)
                ? concurrency.MaxActive
                : 0;
        }

        private sealed class CallbackConcurrency
        {
            private int _active;
            private int _maxActive;

            public int MaxActive => Volatile.Read(ref _maxActive);

            public void Enter()
            {
                var active = Interlocked.Increment(ref _active);
                while (true)
                {
                    var current = Volatile.Read(ref _maxActive);
                    if (active <= current
                        || Interlocked.CompareExchange(ref _maxActive, active, current) == current)
                    {
                        break;
                    }
                }
            }

            public void Leave()
            {
                Interlocked.Decrement(ref _active);
            }
        }

        private sealed class CallbackLease(CallbackConcurrency concurrency) : IDisposable
        {
            public void Dispose()
            {
                concurrency.Leave();
            }
        }
    }

    public sealed class HeaderStreamSession(HeaderStreamRecorder recorder) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; set; } = default!;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            recorder.LastSessionId = Context.SessionId;
            recorder.LastRoutingId = Context.RoutingId;
            recorder.LastLocalAddr = Context.LocalAddr;
            recorder.LastRemoteAddr = Context.RemoteAddr;
            recorder.ConnectedCount++;
            recorder.ConnectedSessionIds.Add(Context.SessionId);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            recorder.DisconnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            recorder.LastError = error;
            recorder.ErrorCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message payload,
            CancellationToken cancellationToken)
        {
            using var lease = recorder.EnterCallback(Context.SessionId);
            _ = cancellationToken;
            _ = header;
            recorder.ReceivedPayloads.Add(Encoding.UTF8.GetString(payload.AsReadOnlySpan()).Trim('"'));
            if (recorder.ReceivedPayloads.Contains("close"))
            {
                return Context.CloseAsync(cancellationToken);
            }

            return Context.Reply("pong")
                .Submit(cancellationToken);
        }
    }

    public sealed record GatewayPing(string Value);

    public sealed record GatewayPong(string Value, ulong RequestSeq);

    public sealed class ActorPlayRouteStore(RoutingId playRid) : IZLinkActorPlayRouteResolver
    {
        public ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
            string actorId,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            _ = actorId;
            return ValueTask.FromResult(new ZLinkActorRoute("gateway", playRid));
        }
    }

    public sealed class ActorDispatchRecorder
    {
        public string? LastPacketName { get; set; }

        public string? LastTraceId { get; set; }

        public bool ForwardedTenantId { get; set; }
    }

    public sealed class ActorSessionLocationStore
        : IZLinkActorSessionBindingStore
    {
        private ZLinkActorSessionBinding? _binding;

        public ValueTask BindSessionAsync(
            ZLinkActorSessionBinding binding,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            _binding = binding;
            return ValueTask.CompletedTask;
        }

        public ValueTask UnbindSessionAsync(
            ZLinkActorSessionUnbind binding,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_binding is { } current
                && current.ActorId == binding.ActorId
                && current.BindingToken == binding.BindingToken)
            {
                _binding = null;
            }

            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
            string actorId,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var binding = _binding
                ?? throw new InvalidOperationException("No session binding exists.");
            return ValueTask.FromResult(new ZLinkActorSessionRoute(
                binding.SessionRouterId,
                binding.BindingToken));
        }
    }

    public sealed class GatewayActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; set; } = default!;

        public void Configure()
        {
            Context.AddPacket<GatewayActorHandler>("relay.echo");
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class GatewayActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new GatewayActor(actorId));
        }
    }

    public sealed class GatewayRelaySession : IZLinkSession
    {
        private IZLinkActorRef? _actor;

        public IZLinkSessionContext Context { get; set; } = default!;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _actor = null;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            _ = error;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            Message payload,
            CancellationToken cancellationToken)
        {
            _actor ??= await Context.BindActorHandleAsync(
                "player-1",
                "player",
                cancellationToken);

            await Context.DispatchToActorAsync(
                    _actor ?? throw new InvalidOperationException("Actor was not created."),
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class GatewayActorHandler(ActorDispatchRecorder recorder)
        : IZLinkActorRequestHandler<GatewayPing, GatewayPong>
    {
        public ValueTask<GatewayPong> HandleAsync(
            GatewayPing request,
            ZLinkActorRequestContext context,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            recorder.LastPacketName = context.PacketName;
            if (context.Metadata.TryGetApplicationValue("trace-id", out var traceId))
            {
                recorder.LastTraceId = traceId;
            }

            recorder.ForwardedTenantId = context.Metadata.TryGetApplicationValue("tenant-id", out _);
            return ValueTask.FromResult(new GatewayPong($"play:{request.Value}", 101));
        }
    }

    private sealed class CallbackExceptionCapture : IDisposable
    {
        private readonly ConcurrentQueue<Exception> _exceptions = new();
        private readonly EventInfo _eventInfo;
        private readonly Action<Exception> _handlerDelegate;

        private CallbackExceptionCapture()
        {
            _eventInfo = typeof(global::Systems.Zlink.Context).Assembly
                .GetType("Systems.Zlink.Runtime", throwOnError: true)!
                .GetEvent("UnhandledCallbackException", BindingFlags.Public | BindingFlags.Static)!
                ?? throw new InvalidOperationException("Could not locate Systems.Zlink.Runtime.UnhandledCallbackException.");
            _handlerDelegate = OnUnhandledCallbackException;
            _eventInfo.AddEventHandler(null, _handlerDelegate);
        }

        public bool IsEmpty => _exceptions.IsEmpty;

        public static CallbackExceptionCapture Start()
        {
            return new CallbackExceptionCapture();
        }

        public void Dispose()
        {
            _eventInfo.RemoveEventHandler(null, _handlerDelegate);
        }

        public void ThrowIfAny()
        {
            if (_exceptions.IsEmpty)
            {
                return;
            }

            throw new AggregateException(_exceptions);
        }

        private void OnUnhandledCallbackException(Exception exception)
        {
            _exceptions.Enqueue(exception);
        }
    }
}
