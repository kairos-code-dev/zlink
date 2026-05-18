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
        var headerCodec = ZLinkStreamProtocolDefaults.HeaderCodec;
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
    public async Task ActorManager_CreateAsync_Throws_When_ActorAlreadyExists()
    {
        var host = await CreateHostAsync("actor-manager", services =>
        {
            services.AddSingleton<ActorDispatchRecorder>();
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();
            var recorder = host.Services.GetRequiredService<ActorDispatchRecorder>();

            var created = await actors.CreateAsync("actor-duplicate", "player");
            var found = await actors.FindAsync("actor-duplicate");
            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => actors.CreateAsync("actor-duplicate", "player").AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorAlreadyExists, ex.Kind);
            Assert.Same(created, found);
            Assert.Equal(1, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task ActorManager_GetOrCreateAsync_Reuses_ExistingActor_And_Rejects_TypeMismatch()
    {
        var host = await CreateHostAsync("actor-manager", services =>
        {
            services.AddSingleton<ActorDispatchRecorder>();
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddActorFactory<GatewayActorFactory>("spectator");
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();
            var recorder = host.Services.GetRequiredService<ActorDispatchRecorder>();

            var created = await actors.GetOrCreateAsync("actor-reuse", "player");
            var reused = await actors.GetOrCreateAsync("actor-reuse", "player");
            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => actors.GetOrCreateAsync("actor-reuse", "spectator").AsTask());

            Assert.Same(created, reused);
            Assert.Equal(ZLinkFrameworkErrorKind.ActorTypeMismatch, ex.Kind);
            Assert.Equal(1, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task ActorManager_CreateAsync_Clears_State_When_Configure_Fails()
    {
        var recorder = new ConfigureFailureRecorder();
        var host = await CreateHostAsync("actor-manager", services =>
        {
            services.AddSingleton(recorder);
            services.AddScoped<ConfigureFailureActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<ConfigureFailureActorFactory>("player");
            });
        });

        try
        {
            var actors = host.Services.GetRequiredService<IZLinkActorManager>();

            var ex = await Assert.ThrowsAsync<InvalidOperationException>(
                () => actors.CreateAsync("actor-configure-retry", "player").AsTask());
            recorder.FailConfigure = false;
            var foundAfterFailure = await actors.FindAsync("actor-configure-retry");
            var createdAfterRetry = await actors.CreateAsync("actor-configure-retry", "player");

            Assert.Equal("configure failed", ex.Message);
            Assert.Null(foundAfterFailure);
            Assert.Equal("actor-configure-retry", createdAfterRetry.ActorId);
            Assert.Equal(2, recorder.CreatedCount);
            Assert.Equal(2, recorder.ConfigureCount);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task BindActorHandleAsync_DoesNot_Create_LocalActor()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new ActorDispatchRecorder();
        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(endpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = RoutingId.FromString("1001"));
                    routed.UseManualConnections(connections => connections.Connect(endpoint));
                });
            });
        });

        try
        {
            var context = new ZLinkSessionContext(
                host.Services.GetRequiredService<ZLinkFrameworkRuntime>(),
                host.Services.GetRequiredService<IZLinkClient>(),
                new SpotIntegrationTests.TestStream("missing-actor-session"),
                ZLinkStreamProtocolDefaults.HeaderCodec,
                static _ => ValueTask.CompletedTask,
                static _ => ValueTask.CompletedTask);

            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => context.BindActorHandleAsync("missing-player", "player").AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
            Assert.Equal(0, recorder.CreatedCount);
        }
        finally
        {
            await host.StopAsync();
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
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Payload));

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
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(secondReply.Payload));

            Assert.NotNull(recorder.LastSessionId);
            Assert.Equal(recorder.LastSessionId, recorder.ConstructorContextSessionId);
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
    public async Task HeaderStreamSession_Uses_Configured_HeaderCodec()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        var headerCodec = new PrefixStreamHeaderCodec(0x7a);
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.UseHeaderCodec(headerCodec);
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
                    new ZlinkStreamRequestSeq(11),
                    "ping",
                    ZlinkStreamMetadata.Empty),
                "\"custom\""u8,
                headerCodec));

            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("custom") && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();

            var reply = ReceiveFrame(network, new ZlinkStreamRequestSeq(11), headerCodec);
            Assert.Equal(ZlinkStreamMessageKind.Response, reply.Header.Kind);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Payload));
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Responds_To_Heartbeat_Control_Ping()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();

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
                    ZlinkStreamMessageKind.Control,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "$zlink.heartbeat.ping",
                    ZlinkStreamMetadata.Empty),
                ReadOnlySpan<byte>.Empty));

            var reply = ReceiveFrame(network);
            Assert.Equal(ZlinkStreamMessageKind.Control, reply.Header.Kind);
            Assert.Equal("$zlink.heartbeat.pong", reply.Header.Name);
            Assert.Empty(reply.Payload);
            Assert.Empty(recorder.ReceivedPayloads);
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
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
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
        await playHost.Services.GetRequiredService<IZLinkActorManager>()
            .GetOrCreateAsync("player-1", "player");

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new ActorPlayRouteStore(playRid));
            services.AddSingleton(new GatewaySessionRecorder());
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
            var relayBody = JsonSerializer.Deserialize<GatewayPong>(relayReply.Payload, JsonOptions);
            Assert.Equal(ZlinkStreamMessageKind.Response, relayReply.Header.Kind);
            Assert.Equal("play:from-client", relayBody?.Value);
            Assert.Equal(101UL, relayBody?.RequestSeq);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-101", proxyRecorder.LastTraceId);
            Assert.False(proxyRecorder.ForwardedTenantId);

            var sessionProxy = playHost.Services.GetRequiredService<IZLinkActorSessionClient>();
            var clientReplyTask = Task.Run(() =>
            {
                var request = ReceiveFrame(network);
                Assert.Equal(ZlinkStreamMessageKind.Request, request.Header.Kind);
                Assert.Equal("client.echo", request.Header.Name);
                Assert.NotNull(request.Header.RequestSeq);
                var ping = JsonSerializer.Deserialize<GatewayPing>(request.Payload, JsonOptions);
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

            await sessionProxy.Send(
                    "player-1",
                    new GatewayPing("one-way-from-play"))
                .PacketName("client.notify")
                .Submit();
            var clientPush = ReceiveFrame(network);
            Assert.Equal(ZlinkStreamMessageKind.Send, clientPush.Header.Kind);
            Assert.Equal("client.notify", clientPush.Header.Name);
            var clientPushBody = JsonSerializer.Deserialize<GatewayPing>(clientPush.Payload, JsonOptions);
            Assert.Equal("one-way-from-play", clientPushBody?.Value);

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(102),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty
                        .With("trace-id", "trace-session-relay")),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("from-session-relay"), JsonOptions)));
            var secondRelayReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(102));
            var secondRelayBody = JsonSerializer.Deserialize<GatewayPong>(secondRelayReply.Payload, JsonOptions);

            Assert.Equal("play:from-session-relay", secondRelayBody?.Value);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-session-relay", proxyRecorder.LastTraceId);
            callbackCapture.ThrowIfAny();

            client.Dispose();
            await RetryAsync(
                () => proxyRecorder.DisconnectedCount > 0 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }

    [Fact]
    public async Task RemoteActorDispatch_DoesNot_Create_MissingActor()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString("0707");
        var playRid = RoutingId.FromString("0808");
        var actorRecorder = new ActorDispatchRecorder();
        var sessionLocations = new ActorSessionLocationStore();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddSingleton(sessionLocations);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddZLinkFramework(options =>
            {
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
            services.AddSingleton(new GatewaySessionRecorder());
            services.AddSingleton(sessionLocations);
            services.AddScoped<MissingRemoteActorRelaySession>();
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
                    stream.AddHeaderSession<MissingRemoteActorRelaySession>();
                });
            });
        });

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(301),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("missing-remote"), JsonOptions)));

            var error = ReceiveFrame(network, new ZlinkStreamRequestSeq(301));
            Assert.Equal(ZlinkStreamMessageKind.Error, error.Header.Kind);
            Assert.Equal(0, actorRecorder.CreatedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }

    [Fact]
    public async Task ActorRefNotifyDisconnected_Notifies_Local_Bound_Actor()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var routerEndpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("0303");
        var recorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        var sessionLocations = new ActorSessionLocationStore();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(routerEndpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddSingleton(sessionRecorder);
            services.AddSingleton(sessionLocations);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddScoped<LocalNotifyDisconnectSession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddActorSessionBindingStore<ActorSessionLocationStore>();
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(routerEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(routerEndpoint));
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AddHeaderSession<LocalNotifyDisconnectSession>();
                });
            });
        });

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();
            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "open",
                    ZlinkStreamMetadata.Empty),
                "\"open\""u8));

            await RetryAsync(
                () => recorder.CreatedCount == 1 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();

            client.Dispose();
            await RetryAsync(
                () => recorder.DisconnectedCount > 0 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task SessionProxyDisconnect_FromLocalActor_Closes_Client_Without_Session_Disconnect_Callback()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var routerEndpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("0404");
        var actorRecorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        var sessionLocations = new ActorSessionLocationStore();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(routerEndpoint, services =>
        {
            services.AddSingleton(new ActorPlayRouteStore(localRid));
            services.AddSingleton(actorRecorder);
            services.AddSingleton(sessionRecorder);
            services.AddSingleton(sessionLocations);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<LocalNotifyDisconnectSession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddActorSessionBindingStore<ActorSessionLocationStore>();
                options.AddActorPlayRouteResolver<ActorPlayRouteStore>();
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(routerEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(routerEndpoint));
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AddHeaderSession<LocalNotifyDisconnectSession>();
                });
            });
        });

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();
            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "open",
                    ZlinkStreamMetadata.Empty),
                "\"open\""u8));
            await RetryAsync(
                () => actorRecorder.CreatedCount == 1
                    && sessionLocations.HasBinding
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "session.disconnect",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("close-local"), JsonOptions)));

            await RetryAsync(
                () => actorRecorder.ProxyDisconnectCount == 1
                    && !sessionLocations.HasBinding
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, sessionRecorder.DisconnectedCount);
            Assert.Equal(0, actorRecorder.DisconnectedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task SessionProxyDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString("0505");
        var playRid = RoutingId.FromString("0606");
        var actorRecorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        var sessionLocations = new ActorSessionLocationStore();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddSingleton(sessionLocations);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddZLinkFramework(options =>
            {
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
        await playHost.Services.GetRequiredService<IZLinkActorManager>()
            .GetOrCreateAsync("player-1", "player");

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new ActorPlayRouteStore(playRid));
            services.AddSingleton(sessionRecorder);
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

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(201),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("bind-remote"), JsonOptions)));
            var bindReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(201));
            var bindBody = JsonSerializer.Deserialize<GatewayPong>(bindReply.Payload, JsonOptions);
            Assert.Equal("play:bind-remote", bindBody?.Value);
            await RetryAsync(
                () => actorRecorder.CreatedCount == 1
                    && sessionLocations.HasBinding
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(202),
                    "session.disconnect",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("close-remote"), JsonOptions)));
            var disconnectReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(202));
            var disconnectBody = JsonSerializer.Deserialize<GatewayPong>(disconnectReply.Payload, JsonOptions);
            Assert.Equal("disconnect:close-remote", disconnectBody?.Value);

            await RetryAsync(
                () => actorRecorder.ProxyDisconnectCount == 1
                    && !sessionLocations.HasBinding
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, sessionRecorder.DisconnectedCount);
            Assert.Equal(0, actorRecorder.DisconnectedCount);
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
        ReadOnlySpan<byte> payload,
        IZlinkStreamHeaderCodec? headerCodec = null)
    {
        var headerBytes = (headerCodec ?? ZLinkStreamProtocolDefaults.HeaderCodec).Encode(header).ToArray();
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

    private static (ZlinkStreamHeader Header, byte[] Payload) ReceiveFrame(
        NetworkStream stream,
        IZlinkStreamHeaderCodec? headerCodec = null)
    {
        var lengths = ReceiveExact(stream, 6);
        var headerLength = (lengths[0] << 8) | lengths[1];
        var payloadLength = (lengths[2] << 24) | (lengths[3] << 16) | (lengths[4] << 8) | lengths[5];
        var headerBytes = ReceiveExact(stream, headerLength);
        var payloadBytes = ReceiveExact(stream, payloadLength);
        var header = (headerCodec ?? ZLinkStreamProtocolDefaults.HeaderCodec).Decode(headerBytes);
        return (header, payloadBytes);
    }

    private static (ZlinkStreamHeader Header, byte[] Payload) ReceiveFrame(
        NetworkStream stream,
        ZlinkStreamRequestSeq requestSeq,
        IZlinkStreamHeaderCodec? headerCodec = null)
    {
        while (true)
        {
            var frame = ReceiveFrame(stream, headerCodec);
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

        public string? ConstructorContextSessionId { get; set; }

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

    public sealed class HeaderStreamSession : IZLinkSession
    {
        private readonly HeaderStreamRecorder _recorder;
        private readonly IZLinkSessionContext _context;

        public HeaderStreamSession(
            HeaderStreamRecorder recorder,
            IZLinkSessionContext context)
        {
            _recorder = recorder;
            _context = context;
            recorder.ConstructorContextSessionId = context.SessionId;
        }

        public IZLinkSessionContext Context => _context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _recorder.LastSessionId = _context.SessionId;
            _recorder.LastRoutingId = _context.RoutingId;
            _recorder.LastLocalAddr = _context.LocalAddr;
            _recorder.LastRemoteAddr = _context.RemoteAddr;
            _recorder.ConnectedCount++;
            _recorder.ConnectedSessionIds.Add(_context.SessionId);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _recorder.DisconnectedCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _recorder.LastError = error;
            _recorder.ErrorCount++;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message payload,
            CancellationToken cancellationToken)
        {
            using var lease = _recorder.EnterCallback(_context.SessionId);
            _ = cancellationToken;
            _ = header;
            _recorder.ReceivedPayloads.Add(Encoding.UTF8.GetString(payload.AsReadOnlySpan()).Trim('"'));
            if (_recorder.ReceivedPayloads.Contains("close"))
            {
                return _context.CloseAsync(cancellationToken);
            }

            return _context.Reply("pong")
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
        private int _createdCount;
        private int _disconnectedCount;
        private int _proxyDisconnectCount;

        public string? LastPacketName { get; set; }

        public string? LastTraceId { get; set; }

        public bool ForwardedTenantId { get; set; }

        public int CreatedCount => Volatile.Read(ref _createdCount);

        public int DisconnectedCount => Volatile.Read(ref _disconnectedCount);

        public int ProxyDisconnectCount => Volatile.Read(ref _proxyDisconnectCount);

        public void RecordCreated()
        {
            Interlocked.Increment(ref _createdCount);
        }

        public void RecordDisconnected()
        {
            Interlocked.Increment(ref _disconnectedCount);
        }

        public void RecordProxyDisconnect()
        {
            Interlocked.Increment(ref _proxyDisconnectCount);
        }
    }

    public sealed class ActorSessionLocationStore
        : IZLinkActorSessionBindingStore
    {
        private readonly object _gate = new();
        private ZLinkActorSessionBinding? _binding;

        public bool HasBinding
        {
            get
            {
                lock (_gate)
                {
                    return _binding is not null;
                }
            }
        }

        public ValueTask BindSessionAsync(
            ZLinkActorSessionBinding binding,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                _binding = binding;
            }

            return ValueTask.CompletedTask;
        }

        public ValueTask UnbindSessionAsync(
            ZLinkActorSessionUnbind binding,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            lock (_gate)
            {
                if (_binding is { } current
                    && current.ActorId == binding.ActorId
                    && current.BindingToken == binding.BindingToken)
                {
                    _binding = null;
                }
            }

            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
            string actorId,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            ZLinkActorSessionBinding binding;
            lock (_gate)
            {
                binding = _binding
                    ?? throw new InvalidOperationException("No session binding exists.");
            }

            return ValueTask.FromResult(new ZLinkActorSessionRoute(
                binding.SessionRouterId,
                binding.BindingToken));
        }
    }

    public sealed class GatewayActor(
        string actorId,
        IZLinkActorContext context,
        ActorDispatchRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public ActorDispatchRecorder Recorder { get; } = recorder;

        public void Configure()
        {
            Context.AddPacket<GatewayActorHandler>("relay.echo");
            Context.AddPacket<GatewaySessionDisconnectHandler>("session.disconnect");
            Context.AddPacket<GatewaySessionDisconnectRequestHandler>("session.disconnect");
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Recorder.RecordDisconnected();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class GatewayActorFactory(ActorDispatchRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            recorder.RecordCreated();
            return ValueTask.FromResult<IZLinkActor>(new GatewayActor(actorId, context, recorder));
        }
    }

    public sealed class ConfigureFailureRecorder
    {
        private int _createdCount;
        private int _configureCount;

        public bool FailConfigure { get; set; } = true;

        public int CreatedCount => Volatile.Read(ref _createdCount);

        public int ConfigureCount => Volatile.Read(ref _configureCount);

        public void RecordCreated()
        {
            Interlocked.Increment(ref _createdCount);
        }

        public void RecordConfigure()
        {
            Interlocked.Increment(ref _configureCount);
        }
    }

    public sealed class ConfigureFailureActor(
        string actorId,
        IZLinkActorContext context,
        ConfigureFailureRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public void Configure()
        {
            recorder.RecordConfigure();
            if (recorder.FailConfigure)
            {
                throw new InvalidOperationException("configure failed");
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ConfigureFailureActorFactory(ConfigureFailureRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            recorder.RecordCreated();
            return ValueTask.FromResult<IZLinkActor>(
                new ConfigureFailureActor(actorId, context, recorder));
        }
    }

    public sealed class GatewayRelaySession(
        GatewaySessionRecorder recorder,
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkActorRef? _actor;

        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            recorder.RecordDisconnected();
            var actor = _actor;
            _actor = null;
            if (actor is not null)
            {
                await actor.NotifyDisconnectedAsync(cancellationToken).ConfigureAwait(false);
            }
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

            await Context.RelayToActorAsync(
                    _actor ?? throw new InvalidOperationException("Actor was not created."),
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class MissingRemoteActorRelaySession(IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkActorRef? _actor;

        public IZLinkSessionContext Context { get; } = context;

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

            await Context.RelayToActorAsync(
                    _actor,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class LocalNotifyDisconnectSession(
        GatewaySessionRecorder recorder,
        IZLinkActorManager actors,
        IZLinkSessionContext context) : IZLinkSession
    {
        private IZLinkActorRef? _actor;

        public IZLinkSessionContext Context { get; } = context;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            recorder.RecordDisconnected();
            var actor = _actor;
            _actor = null;
            if (actor is not null)
            {
                await actor.NotifyDisconnectedAsync(cancellationToken).ConfigureAwait(false);
            }
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
            _ = header;
            using (payload)
            {
                if (_actor is null)
                {
                    await actors.GetOrCreateAsync(
                            "local-player-1",
                            "player",
                            cancellationToken)
                        .ConfigureAwait(false);

                    _actor = await Context.BindActorHandleAsync(
                            "local-player-1",
                            "player",
                            cancellationToken)
                        .ConfigureAwait(false);
                }

                if (!string.Equals(header.Name, "open", StringComparison.Ordinal))
                {
                    using var dispatchPayload = payload.Move();
                    await Context.RelayToActorAsync(
                            _actor,
                            header,
                            dispatchPayload,
                            cancellationToken)
                        .ConfigureAwait(false);
                }
            }
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

    public sealed class GatewaySessionDisconnectHandler(ActorDispatchRecorder recorder)
        : IZLinkActorSendHandler<GatewayPing>
    {
        public async ValueTask HandleAsync(
            GatewayPing message,
            ZLinkActorSendContext context,
            CancellationToken cancellationToken)
        {
            _ = message;
            recorder.RecordProxyDisconnect();
            await context.SessionProxy.DisconnectAsync(cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class GatewaySessionDisconnectRequestHandler(ActorDispatchRecorder recorder)
        : IZLinkActorRequestHandler<GatewayPing, GatewayPong>
    {
        public async ValueTask<GatewayPong> HandleAsync(
            GatewayPing request,
            ZLinkActorRequestContext context,
            CancellationToken cancellationToken)
        {
            recorder.RecordProxyDisconnect();
            await context.SessionProxy.DisconnectAsync(cancellationToken)
                .ConfigureAwait(false);
            return new GatewayPong($"disconnect:{request.Value}", 202);
        }
    }

    public sealed class GatewaySessionRecorder
    {
        private int _disconnectedCount;

        public int DisconnectedCount => Volatile.Read(ref _disconnectedCount);

        public void RecordDisconnected()
        {
            Interlocked.Increment(ref _disconnectedCount);
        }
    }

    private sealed class PrefixStreamHeaderCodec(byte prefix) : IZlinkStreamHeaderCodec
    {
        private readonly IZlinkStreamHeaderCodec _inner = ZLinkStreamProtocolDefaults.HeaderCodec;

        public ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header)
        {
            var encoded = _inner.Encode(header);
            var bytes = new byte[encoded.Length + 1];
            bytes[0] = prefix;
            encoded.CopyTo(bytes.AsMemory(1));
            return bytes;
        }

        public ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header)
        {
            if (header.Length == 0 || header.Span[0] != prefix)
            {
                throw new InvalidOperationException("Unexpected stream header prefix.");
            }

            return _inner.Decode(header[1..]);
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
