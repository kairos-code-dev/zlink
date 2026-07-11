using System.Diagnostics;
using System.Collections.Concurrent;
using System.Diagnostics.Metrics;
using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests;

public sealed class UnhandledDispatchPolicyTests
{
    [Fact]
    public async Task SpotSubscription_MalformedUnknownTopic_ReportsInvalidFrameBeforeTopicLookup()
    {
        var result = await ObserveUnknownSpotSubscriptionAsync(malformed: true);

        Assert.Equal(ZLinkMessageFlowOutcome.Error, result.Error.Outcome);
        Assert.Equal(ZLinkDispatchErrorSurface.SpotSubscription, result.Error.Surface);
        Assert.Equal(ZLinkDispatchErrorReason.InvalidFrame, result.Error.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, result.Error.ErrorAction);
        Assert.Equal(TestFlowId, result.Error.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, result.Error.FlowOrigin);
        Assert.Equal(nameof(TestSubscriptionEvent), result.Error.PacketName);
        Assert.Equal("events.child", result.Error.Topic);
        Assert.Equal("subscription-correlation", result.Error.CorrelationId);
        Assert.Equal("source-rid", result.Error.SourceRid);
        Assert.Equal(0, result.DispatchCount);
        Assert.Equal(0, result.FanoutReceived);
        Assert.Empty(result.LogMessages);
    }

    [Fact]
    public async Task SpotSubscription_OffModeUnknownTopic_PreservesHeaderAndInboundFlow()
    {
        var result = await ObserveUnknownSpotSubscriptionAsync(malformed: false);

        Assert.Equal(ZLinkMessageFlowOutcome.Received, result.Received!.Outcome);
        Assert.Equal(TestFlowId, result.Received.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, result.Received.FlowOrigin);
        Assert.Equal(nameof(TestSubscriptionEvent), result.Received.PacketName);
        Assert.Equal("events.child", result.Received.Topic);
        Assert.Equal("subscription-correlation", result.Received.CorrelationId);
        Assert.Equal("source-rid", result.Received.SourceRid);

        Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, result.Error.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, result.Error.ErrorAction);
        Assert.Equal(TestFlowId, result.Error.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, result.Error.FlowOrigin);
        Assert.Equal(nameof(TestSubscriptionEvent), result.Error.PacketName);
        Assert.Equal("events.child", result.Error.Topic);
        Assert.Equal("subscription-correlation", result.Error.CorrelationId);
        Assert.Equal("source-rid", result.Error.SourceRid);
        Assert.Equal(0, result.DispatchCount);
        Assert.True(result.FanoutReceived > 0);
        Assert.Empty(result.LogMessages);
    }

    [Fact]
    public async Task ActorJoinMissingHandler_OffMode_KeepsTelemetryButSuppressesLog()
    {
        using var listener = new ActivityListener
        {
            ShouldListenTo = source => source.Name == ZLinkTelemetry.ActivitySourceName,
            Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded
        };
        var activities = new List<Activity>();
        listener.ActivityStopped = activities.Add;
        ActivitySource.AddActivityListener(listener);
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        var nativeSpot = new CapturingSpot();
        var logger = new CapturingLogger<ZLinkSpotActorJoinDispatcher>();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            null!,
            nativeSpot,
            "entry",
            new ZLinkSpotActorJoinRegistry(),
            new ZLinkSpotActorMembership(),
            static () => throw new InvalidOperationException("Handler invoker should not be used."),
            logger,
            dispatchErrors: new ZLinkDispatchErrorReporter(options));
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "entry",
                "JoinReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null),
            new { Value = "join" },
            typeof(object),
            null);
        var request = new ZLinkBackendActorJoinRequest(
            new ZLinkBackendActorRef(RoutingId.From("source-node"), "source-actor", 1),
            new ZLinkBackendActorRef(RoutingId.From("target-node"), "target-actor", 1),
            RoutingId.From("source-node"),
            RoutingId.From("target-spot"),
            1,
            parts[0],
            parts);

        try
        {
            await dispatcher.DispatchAsync(request, CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        Assert.Equal(1, nativeSpot.LastJoinResultCode);
        Assert.Empty(logger.Messages);
        Assert.Contains(activities, activity => activity.OperationName == "zlink.actor.dispatch");
    }

    [Fact]
    public async Task ActorJoinMissingHandler_RepliesRejected_AndLogsReason()
    {
        var nativeSpot = new CapturingSpot();
        var logger = new CapturingLogger<ZLinkSpotActorJoinDispatcher>();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            null!,
            nativeSpot,
            "entry",
            new ZLinkSpotActorJoinRegistry(),
            new ZLinkSpotActorMembership(),
            static () => throw new InvalidOperationException("Handler invoker should not be used."),
            logger);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "entry",
                "JoinReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null),
            new { Value = "join" },
            typeof(object),
            null);
        var request = new ZLinkBackendActorJoinRequest(
            new ZLinkBackendActorRef(RoutingId.From("source-node"), "source-actor", 1),
            new ZLinkBackendActorRef(RoutingId.From("target-node"), "target-actor", 1),
            RoutingId.From("source-node"),
            RoutingId.From("target-spot"),
            1,
            parts[0],
            parts);

        try
        {
            await dispatcher.DispatchAsync(request, CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        Assert.Equal(1, nativeSpot.LastJoinResultCode);
        Assert.Contains(logger.Messages, message => message.Contains("no-join-handler", StringComparison.Ordinal));
    }

    [Fact]
    public async Task HandlerMissing_EmitsTrace()
    {
        using var listener = new ActivityListener
        {
            ShouldListenTo = source => source.Name == ZLinkTelemetry.ActivitySourceName,
            Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded
        };
        var activities = new List<Activity>();
        listener.ActivityStopped = activities.Add;
        ActivitySource.AddActivityListener(listener);
        var nativeSpot = new CapturingSpot();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            null!,
            nativeSpot,
            "entry",
            new ZLinkSpotActorJoinRegistry(),
            new ZLinkSpotActorMembership(),
            static () => throw new InvalidOperationException("Handler invoker should not be used."),
            new CapturingLogger<ZLinkSpotActorJoinDispatcher>());
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "entry",
                "JoinReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null),
            new { Value = "join" },
            typeof(object),
            null);
        var request = new ZLinkBackendActorJoinRequest(
            new ZLinkBackendActorRef(RoutingId.From("source-node"), "source-actor", 1),
            new ZLinkBackendActorRef(RoutingId.From("target-node"), "target-actor", 1),
            RoutingId.From("source-node"),
            RoutingId.From("target-spot"),
            1,
            parts[0],
            parts);

        try
        {
            await dispatcher.DispatchAsync(request, CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        Assert.Contains(activities, activity =>
            activity.OperationName == "zlink.actor.dispatch"
            && activity.Tags.Any(tag =>
                tag.Key == "zlink.reason" && tag.Value == "no-join-handler"));
    }

    [Fact]
    public async Task ChannelRequestPayloadDecodeFailure_RepliesError()
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection().BuildServiceProvider();
        var registry = new ZLinkHandlerRegistry([
            new ZLinkHandlerEndpointDescriptor(
                ZLinkMessageKind.Request,
                "BrokenReq",
                typeof(NeverInvokedHandler),
                static (_, _, _, _, _, _) => null,
                [],
                typeof(TestRequest),
                typeof(TestReply),
                typeof(ZLinkRequestContext),
                false,
                new HashSet<string>(StringComparer.Ordinal),
                "play")
        ]);
        var dispatcher = new ZLinkChannelPacketDispatcher(
            registry,
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            registration,
            null!);
        var endpoint = GetTcpEndpoint();
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var routerSocket = context.CreateRouterSocket();
        await using var dealerSocket = context.CreateDealerSocket();
        routerSocket.Options.Linger = TimeSpan.Zero;
        dealerSocket.Options.Linger = TimeSpan.Zero;
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        var router = new ZLinkBackendRouterSocketWrapper(routerSocket);
        var requestParts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "play",
                "BrokenReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                "corr",
                null,
                null,
                null,
                null),
            new { Broken = "payload" },
            typeof(object),
            null);
        requestParts[1].Dispose();
        requestParts = [requestParts[0], Message.From("{")];

        var requestTask = dealerSocket.Request()
            .Message(requestParts[0])
            .Message(requestParts[1])
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        ZLinkMessageParts.DisposeAll(requestParts);

        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchServerMessageAsync("play", router, received, CancellationToken.None);

        var reply = await requestTask.WaitAsync(TimeSpan.FromSeconds(2));
        try
        {
            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            Assert.Equal(ZLinkMessageKind.Error, replyHeader.Kind);
            Assert.Equal(nameof(ZLinkFrameworkErrorKind.PayloadDecodeFailed), replyHeader.ErrorCode);
            Assert.Contains("PayloadDecodeFailed", replyHeader.ErrorMessage);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    [Fact]
    public async Task Invalid_Envelope_Marker_Replies_RequestProtocolError()
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection().BuildServiceProvider();
        var dispatcher = new ZLinkChannelPacketDispatcher(
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            registration,
            null);
        var endpoint = GetTcpEndpoint();
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var routerSocket = context.CreateRouterSocket();
        await using var dealerSocket = context.CreateDealerSocket();
        routerSocket.Options.Linger = TimeSpan.Zero;
        dealerSocket.Options.Linger = TimeSpan.Zero;
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        var router = new ZLinkBackendRouterSocketWrapper(routerSocket);
        var invalidHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "BrokenReq",
            ZLinkEnvelopeCodec.DefaultContentType,
            "corr",
            null,
            null,
            null,
            null)
        {
            FlowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d",
            FlowOrigin = ZLinkFlowOrigin.Application
        };
        using var invalidHeaderPart = Message.From(
            System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(
                invalidHeader,
                ZLinkJsonSerializerOptions.Default));
        using var body = Message.From("{}");

        var requestTask = dealerSocket.Request()
            .Message(invalidHeaderPart)
            .Message(body)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchServerMessageAsync("play", router, received, CancellationToken.None);

        var reply = await requestTask.WaitAsync(TimeSpan.FromSeconds(2));
        try
        {
            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            Assert.Equal(ZLinkMessageKind.Error, replyHeader.Kind);
            Assert.Equal(
                nameof(ZLinkFrameworkErrorKind.RequestProtocolError),
                replyHeader.ErrorCode);
            Assert.Contains("format marker", replyHeader.ErrorMessage);
            Assert.Equal(invalidHeader.FlowId, replyHeader.FlowId);
            Assert.Equal(invalidHeader.FlowOrigin, replyHeader.FlowOrigin);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    [Fact]
    public async Task Invalid_Json_Envelope_Replies_RequestProtocolError()
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection().BuildServiceProvider();
        var dispatcher = new ZLinkChannelPacketDispatcher(
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            registration,
            null);
        var endpoint = GetTcpEndpoint();
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var routerSocket = context.CreateRouterSocket();
        await using var dealerSocket = context.CreateDealerSocket();
        routerSocket.Options.Linger = TimeSpan.Zero;
        dealerSocket.Options.Linger = TimeSpan.Zero;
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        var router = new ZLinkBackendRouterSocketWrapper(routerSocket);
        using var invalidHeader = Message.From("{");
        using var body = Message.From("{}");

        var requestTask = dealerSocket.Request()
            .Message(invalidHeader)
            .Message(body)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchServerMessageAsync("play", router, received, CancellationToken.None);

        var reply = await requestTask.WaitAsync(TimeSpan.FromSeconds(2));
        try
        {
            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            Assert.Equal(ZLinkMessageKind.Error, replyHeader.Kind);
            Assert.Equal(nameof(ZLinkFrameworkErrorKind.RequestProtocolError), replyHeader.ErrorCode);
            Assert.Contains("header is invalid", replyHeader.ErrorMessage);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    [Fact]
    public async Task ChannelPublishDecodeFailure_DoesNotStopOtherEndpointTypes()
    {
        var probe = new PublishProbe();
        var services = new ServiceCollection()
            .AddSingleton(probe)
            .AddTransient<CapturingPublishHandler>()
            .BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration();
        var registry = new ZLinkHandlerRegistry([
            new ZLinkHandlerEndpointDescriptor(
                ZLinkMessageKind.Publish,
                "SharedEvent",
                typeof(NeverInvokedHandler),
                static (_, _, _, _, _, _) => null,
                [ZLinkHandlerArgumentKind.Message],
                typeof(int),
                null,
                null,
                false,
                new HashSet<string>(StringComparer.Ordinal),
                "play"),
            new ZLinkHandlerEndpointDescriptor(
                ZLinkMessageKind.Publish,
                "SharedEvent",
                typeof(CapturingPublishHandler),
                static (target, message, _, _, _, _) =>
                    ((CapturingPublishHandler)target).Handle((TestPublishedEvent)message!),
                [ZLinkHandlerArgumentKind.Message],
                typeof(TestPublishedEvent),
                null,
                null,
                false,
                new HashSet<string>(StringComparer.Ordinal),
                "play")
        ]);
        var pipeline = new ZLinkChannelPublishDispatchPipeline(
            registry,
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            static _ => new HashSet<string>(StringComparer.Ordinal),
            LogLevel.Warning,
            new ZLinkDispatchErrorReporter(registration.DispatchOptions),
            registration.Codecs,
            new CapturingLogger<ZLinkChannelPublishDispatchPipeline>());
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            "play",
            "SharedEvent",
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            "events",
            null,
            null);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            header,
            new TestPublishedEvent("delivered"),
            typeof(TestPublishedEvent),
            null);
        var backendFactory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = backendFactory.CreateChannelAdapter();
        await using var context = channelAdapter.CreateContext();
        await using var publisher = channelAdapter.CreatePublisherSocket(context);
        await using var subscriber = channelAdapter.CreateSubscriberSocket(context);
        var endpoint = GetTcpEndpoint();
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetSubscription(string.Empty);
        using var topicMessage = new TopicMessage();

        try
        {
            var timeout = Stopwatch.StartNew();
            var received = false;
            while (timeout.Elapsed < TimeSpan.FromSeconds(2))
            {
                publisher.Publish("events", parts, SendFlags.None);
                if (subscriber.Subscribe(topicMessage, RecvFlags.DontWait))
                {
                    received = true;
                    break;
                }

                await Task.Delay(10);
            }

            Assert.True(received, "The publish test message was not received.");
            await pipeline.DispatchAsync("play", topicMessage, header, CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        Assert.Equal("delivered", probe.Value);
    }

    [Fact]
    public async Task DispatchErrorReporter_DeliversMessageFlowErrorSnapshot()
    {
        var observer = new CapturingMessageFlowObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        options.SetMessageFlowObserver(observer);
        var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var observerPump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var reporter = new ZLinkDispatchErrorReporter(
            options,
            observerPump: observerPump);
        var error = new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Request,
            ZLinkDispatchErrorReason.HandlerMissing,
            ZLinkDispatchErrorAction.ReplyError,
            "MissingReq",
            "api",
            CorrelationId: "corr-1");

        reporter.Report(error);

        var observed = await observer.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(ZLinkMessageFlowOutcome.Error, observed.Outcome);
        Assert.Equal(ZLinkDispatchErrorSurface.Channel, observed.Surface);
        Assert.Equal(ZLinkDispatchMessageKind.Request, observed.MessageKind);
        Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, observed.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.ErrorAction);
        Assert.Equal("MissingReq", observed.PacketName);
        Assert.Equal("api", observed.ChannelName);
        Assert.Equal("corr-1", observed.CorrelationId);
        await runner.StopAsync();
    }

    [Fact]
    public async Task SpotActorSendMissingHandler_LogsAndReportsMessageFlowErrorEvent()
    {
        var observer = new CapturingMessageFlowObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.SetMessageFlowObserver(observer);
        var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var observerPump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var logger = new CapturingLogger<ZLinkSpotActorPacketDispatcher>();
        var dispatcher = new ZLinkSpotActorPacketDispatcher(
            static () => new ZLinkSpotActorHandlerRegistry(ZLinkSpotActorHandlerSurface.UserSpot),
            static () => throw new InvalidOperationException("Handler invoker should not be used."),
            new ZLinkDispatchErrorReporter(
                options,
                observerPump: observerPump),
            logger);
        var actor = new TestActor("actor-1");
        var runtimeState = new ZLinkActorRuntimeState(actor.ActorId);
        runtimeState.BindActorInstance(actor);
        using var body = Message.From("payload");
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            "missing-actor-send",
            ZlinkStreamMetadata.Empty);

        await dispatcher.DispatchAsync(actor, runtimeState, header, body, CancellationToken.None);

        var observed = await observer.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(ZLinkMessageFlowOutcome.Error, observed.Outcome);
        Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, observed.Surface);
        Assert.Equal(ZLinkDispatchMessageKind.ActorSend, observed.MessageKind);
        Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, observed.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, observed.ErrorAction);
        Assert.Equal("missing-actor-send", observed.PacketName);
        Assert.Equal("actor-1", observed.ActorId);
        Assert.Contains(logger.Messages, message => message.Contains("no-handler", StringComparison.Ordinal));
        await runner.StopAsync();
    }

    private static async Task<Received> ReceiveAsync(
        ZLinkBackendRouterSocketWrapper router,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var received = router.Recv(RecvFlags.DontWait);
            if (received is not null) return received;

            await Task.Yield();
        }

        throw new TimeoutException("Timed out waiting for router request.");
    }

    private const string TestFlowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d";

    private static async Task<SpotSubscriptionObservation> ObserveUnknownSpotSubscriptionAsync(
        bool malformed)
    {
        var observer = new CapturingMessageFlowObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        options.SetMessageFlowObserver(observer);
        var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var observerPump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var reporter = new ZLinkDispatchErrorReporter(options, observerPump: observerPump);
        var logger = new CapturingLogger<ZLinkSpotSubscriptionRegistry>();
        var registry = new ZLinkSpotSubscriptionRegistry();
        var nativeSpot = new CapturingSpot();
        registry.Add("events", typeof(TestSubscriptionHandler));
        registry.Bind(new TestSubscriptionSpot(), nativeSpot);

        var backendFactory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = backendFactory.CreateChannelAdapter();
        await using var context = channelAdapter.CreateContext();
        await using var publisher = channelAdapter.CreatePublisherSocket(context);
        await using var subscriber = channelAdapter.CreateSubscriberSocket(context);
        var endpoint = GetTcpEndpoint();
        publisher.Bind(endpoint);
        subscriber.Connect(endpoint);
        subscriber.SetSubscription("events");
        nativeSpot.SubscribeHandler = subscriber.Subscribe;
        long fanoutReceived = 0;
        using var meterListener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.fanout.received")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        meterListener.SetMeasurementEventCallback<long>((instrument, value, tags, _) =>
        {
            if (instrument.Name != "zlink.fanout.received") return;
            foreach (var tag in tags)
                if (tag.Key == "topic" && Equals(tag.Value, "events.child"))
                    Interlocked.Add(ref fanoutReceived, value);
        });
        meterListener.Start();

        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            "play",
            nameof(TestSubscriptionEvent),
            ZLinkEnvelopeCodec.DefaultContentType,
            "subscription-correlation",
            null,
            "events.child",
            null,
            null,
            "source-rid")
        {
            FlowId = TestFlowId,
            FlowOrigin = ZLinkFlowOrigin.Application
        };
        IReadOnlyList<Message> CreateParts()
        {
            if (!malformed)
                return ZLinkEnvelopeCodec.EncodeParts(
                    header,
                    new TestSubscriptionEvent("payload"),
                    typeof(TestSubscriptionEvent),
                    null);

            return
            [
                Message.From(System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(
                    header,
                    ZLinkJsonSerializerOptions.Default)),
                Message.From("{}")
            ];
        }

        var dispatchCount = 0;
        try
        {
            for (var attempt = 0; attempt < 100 && !observer.HasError; attempt++)
            {
                var parts = CreateParts();
                try
                {
                    publisher.Publish("events.child", parts, SendFlags.None);
                }
                finally
                {
                    ZLinkMessageParts.DisposeAll(parts);
                }

                await Task.Delay(5);
                await registry.DrainAsync(
                    nativeSpot,
                    null,
                    reporter,
                    logger,
                    (_, _, _) =>
                    {
                        dispatchCount++;
                        return ValueTask.CompletedTask;
                    },
                    CancellationToken.None);
            }

            var error = await observer.WaitAsync(TimeSpan.FromSeconds(2));
            var received = observer.Events.FirstOrDefault(
                static flow => flow.Outcome == ZLinkMessageFlowOutcome.Received);
            return new SpotSubscriptionObservation(
                received,
                error,
                dispatchCount,
                Interlocked.Read(ref fanoutReceived),
                logger.Messages.ToArray());
        }
        finally
        {
            await runner.StopAsync();
        }
    }

    private static string GetTcpEndpoint()
    {
        using var listener = new TcpListener(
            IPAddress.Loopback,
            0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        return $"tcp://127.0.0.1:{port}";
    }

    private sealed class NeverInvokedHandler;

    private sealed record TestRequest(string Value);

    private sealed record TestReply(string Value);

    private sealed record TestPublishedEvent(string Value);

    private sealed record TestSubscriptionEvent(string Value);

    private sealed class TestSubscriptionSpot : IZLinkSpot
    {
        public IZLinkSpotContext Context => null!;
    }

    private sealed class TestSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<TestSubscriptionSpot, TestSubscriptionEvent>
    {
        public ValueTask HandleAsync(
            TestSubscriptionSpot spot,
            TestSubscriptionEvent message,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed record SpotSubscriptionObservation(
        ZLinkMessageFlowEvent? Received,
        ZLinkMessageFlowEvent Error,
        int DispatchCount,
        long FanoutReceived,
        IReadOnlyList<string> LogMessages);

    private sealed class PublishProbe
    {
        public string? Value { get; set; }
    }

    private sealed class CapturingPublishHandler(PublishProbe probe)
    {
        public ValueTask Handle(TestPublishedEvent message)
        {
            probe.Value = message.Value;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class TestActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context
            => throw new InvalidOperationException("Context is not needed by this test.");
    }

    private sealed class CapturingMessageFlowObserver : IZLinkMessageFlowObserver
    {
        private readonly ConcurrentQueue<ZLinkMessageFlowEvent> _events = new();
        private readonly TaskCompletionSource<ZLinkMessageFlowEvent> _observed =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public IReadOnlyCollection<ZLinkMessageFlowEvent> Events => _events.ToArray();

        public bool HasError => _observed.Task.IsCompleted;

        public ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _events.Enqueue(flow);
            if (flow.Outcome == ZLinkMessageFlowOutcome.Error) _observed.TrySetResult(flow);
            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkMessageFlowEvent> WaitAsync(TimeSpan timeout)
        {
            return await _observed.Task.WaitAsync(timeout);
        }
    }

    private sealed class CapturingLogger<T> : ILogger<T>
    {
        public List<string> Messages { get; } = [];

        public IDisposable? BeginScope<TState>(TState state) where TState : notnull
        {
            return null;
        }

        public bool IsEnabled(LogLevel logLevel)
        {
            return true;
        }

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            Messages.Add(formatter(state, exception));
        }
    }

    private sealed class CapturingSpot : IZLinkBackendSpot
    {
        public Func<TopicMessage, RecvFlags, bool>? SubscribeHandler { get; set; }

        public int? LastJoinResultCode { get; private set; }

        public RoutingId RoutingId => RoutingId.From("spot");

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void SetRoutingId(RoutingId routingId)
        {
        }

        public void SetSubscription(string topic)
        {
        }

        public bool Subscribe(TopicMessage result, RecvFlags flags)
        {
            return SubscribeHandler?.Invoke(result, flags) ?? false;
        }

        public bool RecvRoute(Received result, RecvFlags flags)
        {
            return false;
        }

        public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
        {
        }

        public void OnSendReady(Action handler)
        {
        }

        public bool RequestToChannel(
            string channelName,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public bool RequestToChannel(
            string channelName,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public bool SendToChannel(string channelName, Message message, SendFlags flags)
        {
            return false;
        }

        public bool SendToChannel(string channelName, IReadOnlyList<Message> parts, SendFlags flags)
        {
            return false;
        }

        public bool Publish(string topic, Message message, SendFlags flags)
        {
            return false;
        }

        public bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags)
        {
            return false;
        }

        public bool SendToSpot(RoutingId targetRid, RoutingId targetSpotRid, Message message, SendFlags flags)
        {
            return false;
        }

        public bool SendToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            return false;
        }

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags)
        {
            return null;
        }

        public ZLinkBackendSpotActorLifecycleEvent? RecvActorLifecycle(RecvFlags flags)
        {
            return null;
        }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            Message reply)
        {
            LastJoinResultCode = joinResultCode;
        }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            IReadOnlyList<Message> parts)
        {
            LastJoinResultCode = joinResultCode;
        }

        public void OnActorLifecycle(
            Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
            Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave)
        {
        }
    }
}
