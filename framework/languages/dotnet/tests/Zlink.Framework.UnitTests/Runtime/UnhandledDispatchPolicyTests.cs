using System.Diagnostics;
using System.Collections.Concurrent;
using System.Diagnostics.Metrics;
using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests;

[Collection(RuntimeMetricsCollection.Name)]
public sealed partial class UnhandledDispatchPolicyTests
{
    [Fact]
    public async Task SpotSubscription_MalformedUnknownTopic_ReportsInvalidFrameBeforeTopicLookup()
    {
        var result = await ObserveUnknownSpotSubscriptionAsync(malformed: true);

        Assert.Equal(ZLinkMessageFlowOutcome.Dropped, result.Terminal.Outcome);
        Assert.Equal(ZLinkDispatchErrorSurface.SpotSubscription, result.Terminal.Surface);
        Assert.Equal(ZLinkDispatchErrorReason.InvalidFrame, result.Terminal.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, result.Terminal.ErrorAction);
        Assert.Equal(TestFlowId, result.Terminal.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, result.Terminal.FlowOrigin);
        Assert.Equal(nameof(TestSubscriptionEvent), result.Terminal.PacketName);
        Assert.Equal("events.child", result.Terminal.Topic);
        Assert.Equal("subscription-correlation", result.Terminal.CorrelationId);
        Assert.Equal("source-rid", result.Terminal.SourceRid);
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

        Assert.Equal(ZLinkMessageFlowOutcome.Dropped, result.Terminal.Outcome);
        Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, result.Terminal.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, result.Terminal.ErrorAction);
        Assert.Equal(TestFlowId, result.Terminal.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, result.Terminal.FlowOrigin);
        Assert.Equal(nameof(TestSubscriptionEvent), result.Terminal.PacketName);
        Assert.Equal("events.child", result.Terminal.Topic);
        Assert.Equal("subscription-correlation", result.Terminal.CorrelationId);
        Assert.Equal("source-rid", result.Terminal.SourceRid);
        Assert.Equal(0, result.DispatchCount);
        Assert.True(result.FanoutReceived > 0);
        Assert.Empty(result.LogMessages);
    }

    [Fact]
    public async Task SpotSubscription_Fanout_Instances_Keep_One_Flow_And_Owner_Skip_Uses_The_Same_Identity()
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
        var registryA = new ZLinkSpotSubscriptionRegistry();
        var registryB = new ZLinkSpotSubscriptionRegistry();
        var spotA = new CapturingSpot();
        var spotB = new CapturingSpot();
        var applicationProbe = new TestSubscriptionProbe();
        var owner = new TestSubscriptionSpot(true, applicationProbe);
        var nonOwner = new TestSubscriptionSpot(false, applicationProbe);
        registryA.Add("events.child", typeof(TestSubscriptionHandler));
        registryB.Add("events.child", typeof(TestSubscriptionHandler));
        registryA.Bind(owner, spotA);
        registryB.Bind(nonOwner, spotB);

        var backendFactory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = backendFactory.CreateChannelAdapter();
        await using var context = channelAdapter.CreateContext();
        await using var publisher = channelAdapter.CreatePublisherSocket(context);
        await using var subscriberA = channelAdapter.CreateSubscriberSocket(context);
        await using var subscriberB = channelAdapter.CreateSubscriberSocket(context);
        var endpoint = GetTcpEndpoint();
        publisher.Bind(endpoint);
        subscriberA.Connect(endpoint);
        subscriberB.Connect(endpoint);
        subscriberA.SetSubscription("events.child");
        subscriberB.SetSubscription("events.child");
        spotA.SubscribeHandler = subscriberA.Subscribe;
        spotB.SubscribeHandler = subscriberB.Subscribe;

        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            "play",
            nameof(TestSubscriptionEvent),
            ZLinkEnvelopeCodec.DefaultContentType,
            "fanout-correlation",
            null,
            "events.child",
            null,
            null,
            "source-rid")
        {
            FlowId = TestFlowId,
            FlowOrigin = ZLinkFlowOrigin.Application
        };
        var dispatchA = 0;
        var dispatchB = 0;
        try
        {
            for (var attempt = 0; attempt < 100 && (dispatchA == 0 || dispatchB == 0); attempt++)
            {
                var parts = ZLinkEnvelopeCodec.EncodeParts(
                    header,
                    new TestSubscriptionEvent("payload"),
                    typeof(TestSubscriptionEvent),
                    null);
                try
                {
                    publisher.Publish("events.child", parts, SendFlags.None);
                }
                finally
                {
                    ZLinkMessageParts.DisposeAll(parts);
                }

                await Task.Delay(5);
                await registryA.DrainAsync(
                    spotA,
                    null,
                    reporter,
                    logger,
                    async (_, body, cancellationToken) =>
                    {
                        dispatchA++;
                        await new TestSubscriptionHandler().HandleAsync(
                            owner,
                            Assert.IsType<TestSubscriptionEvent>(body),
                            cancellationToken);
                    },
                    CancellationToken.None);
                await registryB.DrainAsync(
                    spotB,
                    null,
                    reporter,
                    logger,
                    async (_, body, cancellationToken) =>
                    {
                        dispatchB++;
                        await new TestSubscriptionHandler().HandleAsync(
                            nonOwner,
                            Assert.IsType<TestSubscriptionEvent>(body),
                            cancellationToken);
                    },
                    CancellationToken.None);
            }

            Assert.True(dispatchA > 0);
            Assert.True(dispatchB > 0);
            Assert.True(applicationProbe.OwnerHandled > 0);
            Assert.True(applicationProbe.NonOwnerSkipped > 0);
            for (var attempt = 0; attempt < 100
                                  && observer.Events.Count(flow =>
                                      flow.Outcome == ZLinkMessageFlowOutcome.Dispatched) < 2;
                 attempt++)
                await Task.Delay(5);
            var fanout = observer.Events
                .Where(flow => flow.Outcome == ZLinkMessageFlowOutcome.Dispatched)
                .ToArray();
            Assert.True(fanout.Length >= 2);
            Assert.All(fanout, flow =>
            {
                Assert.Equal(TestFlowId, flow.FlowId);
                Assert.Equal(ZLinkFlowOrigin.Application, flow.FlowOrigin);
                Assert.Equal("events.child", flow.Topic);
            });

            Assert.DoesNotContain(
                observer.Events,
                flow => flow.FlowId == TestFlowId
                        && flow.Outcome == ZLinkMessageFlowOutcome.Dropped);
        }
        finally
        {
            await runner.StopAsync();
        }
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
        using var runtimeServices = CreateDispatchRuntime(out var runtime);
        var nativeSpot = new CapturingSpot();
        var logger = new CapturingLogger<ZLinkSpotActorJoinDispatcher>();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            runtime,
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
                "join-request-1",
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
        using var runtimeServices = CreateDispatchRuntime(out var runtime);
        var nativeSpot = new CapturingSpot();
        var logger = new CapturingLogger<ZLinkSpotActorJoinDispatcher>();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            runtime,
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
                "join-request-2",
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
        using var runtimeServices = CreateDispatchRuntime(out var runtime);
        var nativeSpot = new CapturingSpot();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            runtime,
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
                "join-request-3",
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
    public async Task Invalid_Json_Envelope_Without_Correlation_Is_Dropped()
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
            .Timeout(TimeSpan.FromMilliseconds(100))
            .Async();
        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchServerMessageAsync("play", router, received, CancellationToken.None);

        await Assert.ThrowsAsync<ZlinkRequestException>(async () =>
            await requestTask.WaitAsync(TimeSpan.FromSeconds(2)));
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
        var observer = new CapturingMessageFlowObserver();
        registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.Off);
        registration.DispatchOptions.SetMessageFlowObserver(observer);
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var observerPump = new ZLinkMessageFlowObserverPump(registration.DispatchOptions, services, runner);
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
            new ZLinkDispatchErrorReporter(registration.DispatchOptions, observerPump: observerPump),
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

        var timeout = Stopwatch.StartNew();
        var received = false;
        while (timeout.Elapsed < TimeSpan.FromSeconds(2))
        {
            var parts = ZLinkEnvelopeCodec.EncodeParts(
                header,
                new TestPublishedEvent("delivered"),
                typeof(TestPublishedEvent),
                null);
            try
            {
                publisher.Publish("events", parts, SendFlags.None);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            if (subscriber.Subscribe(topicMessage, RecvFlags.DontWait))
            {
                received = true;
                break;
            }

            await Task.Delay(10);
        }

        Assert.True(received, "The publish test message was not received.");
        await pipeline.DispatchAsync("play", topicMessage, header, CancellationToken.None);

        Assert.Equal("delivered", probe.Value);
        var dropped = await observer.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(ZLinkMessageFlowOutcome.Dropped, dropped.Outcome);
        Assert.Equal(ZLinkDispatchErrorReason.PayloadDecodeFailed, dropped.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, dropped.ErrorAction);
        await runner.StopAsync();
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
    public async Task ReplyPathMissing_ReportsFailCallerMessageFlowError()
    {
        var observer = new CapturingMessageFlowObserver();
        var options = new ZLinkDispatchOptionsModel();
        options.SetMessageFlowObserver(observer);
        var services = new ServiceCollection().BuildServiceProvider();
        var runner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), CancellationToken.None);
        await using var observerPump = new ZLinkMessageFlowObserverPump(options, services, runner);
        var reporter = new ZLinkDispatchErrorReporter(options, observerPump: observerPump);
        var scope = new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.SpotActor,
            "SpotActor",
            ZLinkDispatchMessageKind.ActorRequest,
            "ActorRequest",
            "MissingReply",
            actorId: "actor-1");

        scope.ReplyPathMissing(
            NullLogger.Instance,
            reporter,
            new InvalidOperationException("The handler returned no reply."));

        var observed = await observer.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(ZLinkMessageFlowOutcome.Error, observed.Outcome);
        Assert.Equal(ZLinkDispatchErrorReason.ReplyPathMissing, observed.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.FailCaller, observed.ErrorAction);
        Assert.Equal("MissingReply", observed.PacketName);
        Assert.Equal("actor-1", observed.ActorId);
        await runner.StopAsync();
    }

    [Fact]
    public async Task SpotActorSendMissingHandler_LogsAndReportsMessageFlowDroppedEvent()
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
                logger,
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
        Assert.Equal(ZLinkMessageFlowOutcome.Dropped, observed.Outcome);
        Assert.Equal(ZLinkDispatchErrorSurface.SpotActor, observed.Surface);
        Assert.Equal(ZLinkDispatchMessageKind.ActorSend, observed.MessageKind);
        Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, observed.ErrorReason);
        Assert.Equal(ZLinkDispatchErrorAction.Drop, observed.ErrorAction);
        Assert.Equal("missing-actor-send", observed.PacketName);
        Assert.Equal("actor-1", observed.ActorId);
        Assert.Contains(logger.Messages, message => message.Contains("phase=dropped", StringComparison.Ordinal)
                                                    && message.Contains("errorReason=HandlerMissing", StringComparison.Ordinal));
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
            for (var attempt = 0; attempt < 100 && !observer.HasTerminal; attempt++)
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

    private sealed class TestSubscriptionSpot(
        bool ownsMessage = true,
        TestSubscriptionProbe? probe = null) : IZLinkSpot
    {
        public bool OwnsMessage { get; } = ownsMessage;

        public TestSubscriptionProbe? Probe { get; } = probe;

        public IZLinkSpotContext Context => null!;
    }

    private sealed class TestSubscriptionProbe
    {
        public int OwnerHandled;

        public int NonOwnerSkipped;
    }

    private sealed class TestSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<TestSubscriptionSpot, TestSubscriptionEvent>
    {
        public ValueTask HandleAsync(
            TestSubscriptionSpot spot,
            TestSubscriptionEvent message,
            CancellationToken cancellationToken)
        {
            _ = message;
            cancellationToken.ThrowIfCancellationRequested();
            if (!spot.OwnsMessage)
            {
                Interlocked.Increment(ref spot.Probe!.NonOwnerSkipped);
                return ValueTask.CompletedTask;
            }

            if (spot.Probe is not null)
                Interlocked.Increment(ref spot.Probe.OwnerHandled);
            return ValueTask.CompletedTask;
        }
    }

    private sealed record SpotSubscriptionObservation(
        ZLinkMessageFlowEvent? Received,
        ZLinkMessageFlowEvent Terminal,
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

        public bool HasTerminal => _observed.Task.IsCompleted;

        public ValueTask OnMessageFlowAsync(
            ZLinkMessageFlowEvent flow,
            CancellationToken cancellationToken)
        {
            _events.Enqueue(flow);
            if (flow.Outcome is ZLinkMessageFlowOutcome.Error or ZLinkMessageFlowOutcome.Dropped)
                _observed.TrySetResult(flow);
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

    private static ServiceProvider CreateDispatchRuntime(out ZLinkFrameworkRuntime runtime)
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection();
        services.AddSingleton(registration);
        var provider = services.BuildServiceProvider();
        runtime = new ZLinkFrameworkRuntime(
            provider,
            null!,
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                provider.GetRequiredService<IServiceScopeFactory>(),
                registration));
        return provider;
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
