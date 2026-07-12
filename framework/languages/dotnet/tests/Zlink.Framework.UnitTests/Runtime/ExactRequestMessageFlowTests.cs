using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests;

public sealed partial class UnhandledDispatchPolicyTests
{
    [Fact]
    public async Task Channel_Request_Dispatch_Emits_Received_Then_Replied_With_The_Wire_Identity()
    {
        var logger = new CapturingLogger<ZLinkChannelPacketDispatcher>();
        var registration = CreateMessageFlowRegistration();
        using var services = new ServiceCollection()
            .AddTransient<ExactChannelRequestHandler>()
            .BuildServiceProvider();
        var descriptor = ZLinkHandlerEndpointDescriptorFactory.CreateInterface(
            typeof(ExactChannelRequestHandler),
            typeof(IZLinkRequestHandler<ExactRequest, ExactReply>),
            ZLinkMessageKind.Request,
            new HashSet<string>(StringComparer.Ordinal),
            "play",
            "ExactRequest");
        var dispatcher = new ZLinkChannelPacketDispatcher(
            new ZLinkHandlerRegistry([descriptor]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            registration,
            null,
            logger);

        var reply = await DispatchChannelRequestAsync(
            dispatcher,
            CreateHeader("play", "ExactRequest"),
            new ExactRequest("channel"));

        Assert.Equal(
            "CHANNEL",
            Assert.IsType<ExactReply>(ZLinkEnvelopeCodec.DecodeBody(reply, typeof(ExactReply))).Value);
        AssertReceivedThenReplied(logger.Messages, ZLinkDispatchErrorSurface.Channel);
    }

    [Fact]
    public async Task Route_Request_Dispatch_Emits_Received_Then_Replied_With_The_Wire_Identity()
    {
        var logger = new CapturingLogger<ZLinkRoutePacketDispatcher>();
        var registration = CreateMessageFlowRegistration();
        using var services = new ServiceCollection()
            .AddTransient<ExactRouteRequestHandler>()
            .BuildServiceProvider();
        var descriptor = new ZLinkRouteHandlerDescriptor(
            ZLinkMessageKind.Request,
            "mesh",
            "ExactRequest",
            typeof(ExactRouteRequestHandler),
            typeof(ExactRequest),
            typeof(ExactReply),
            ZLinkHandlerMethodInvokerFactory.Create(
                typeof(ExactRouteRequestHandler).GetMethod(
                    nameof(ExactRouteRequestHandler.HandleAsync))!));

        var reply = await DispatchRouteRequestAsync(
            services,
            registration,
            descriptor,
            logger,
            CreateHeader("mesh", "ExactRequest"),
            new ExactRequest("route"));

        Assert.Equal(
            "ROUTE",
            Assert.IsType<ExactReply>(ZLinkEnvelopeCodec.DecodeBody(reply, typeof(ExactReply))).Value);
        AssertReceivedThenReplied(logger.Messages, ZLinkDispatchErrorSurface.RouteMeshChannel);
    }

    [Fact]
    public async Task Channel_Request_Handler_Failure_Logs_The_Inbound_Flow_And_Correlation()
    {
        var logger = new CapturingLogger<ZLinkChannelPacketDispatcher>();
        var registration = CreateMessageFlowRegistration();
        using var services = new ServiceCollection()
            .AddTransient<FailingExactChannelRequestHandler>()
            .BuildServiceProvider();
        var descriptor = ZLinkHandlerEndpointDescriptorFactory.CreateInterface(
            typeof(FailingExactChannelRequestHandler),
            typeof(IZLinkRequestHandler<ExactRequest, ExactReply>),
            ZLinkMessageKind.Request,
            new HashSet<string>(StringComparer.Ordinal),
            "play",
            "FailingExactRequest");
        var dispatcher = new ZLinkChannelPacketDispatcher(
            new ZLinkHandlerRegistry([descriptor]),
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            registration,
            null,
            logger);

        var reply = await DispatchChannelRequestAsync(
            dispatcher,
            CreateHeader("play", "FailingExactRequest"),
            new ExactRequest("failure"));

        Assert.Equal(ZLinkMessageKind.Error, ZLinkEnvelopeCodec.DecodeHeader(reply).Kind);
        var failure = Assert.Single(logger.Messages, line =>
            line.Contains("phase=error", StringComparison.Ordinal));
        Assert.Contains($"flow={ExactFlowId}", failure, StringComparison.Ordinal);
        Assert.Contains($"corr={ExactCorrelationId}", failure, StringComparison.Ordinal);
    }

    private static ZLinkFrameworkRegistration CreateMessageFlowRegistration()
    {
        var registration = new ZLinkFrameworkRegistration();
        registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
        return registration;
    }

    private static ZLinkEnvelopeHeader CreateHeader(string channelName, string packetName)
    {
        return new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            packetName,
            ZLinkEnvelopeCodec.DefaultContentType,
            ExactCorrelationId,
            null,
            null,
            null,
            null)
        {
            FlowId = ExactFlowId,
            FlowOrigin = ZLinkFlowOrigin.Application
        };
    }

    private static async Task<IReadOnlyList<Message>> DispatchChannelRequestAsync(
        ZLinkChannelPacketDispatcher dispatcher,
        ZLinkEnvelopeHeader header,
        ExactRequest request)
    {
        var endpoint = GetTcpEndpoint();
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var routerSocket = context.CreateRouterSocket();
        await using var dealerSocket = context.CreateDealerSocket();
        routerSocket.Options.Linger = TimeSpan.Zero;
        dealerSocket.Options.Linger = TimeSpan.Zero;
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        var router = new ZLinkBackendRouterSocketWrapper(routerSocket);
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, request, typeof(ExactRequest), null);
        var requestTask = dealerSocket.Request()
            .Message(parts[0])
            .Message(parts[1])
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        ZLinkMessageParts.DisposeAll(parts);

        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchServerMessageAsync("play", router, received, CancellationToken.None);
        return await requestTask.WaitAsync(TimeSpan.FromSeconds(2));
    }

    private static async Task<IReadOnlyList<Message>> DispatchRouteRequestAsync(
        IServiceProvider services,
        ZLinkFrameworkRegistration registration,
        ZLinkRouteHandlerDescriptor descriptor,
        ILogger<ZLinkRoutePacketDispatcher> logger,
        ZLinkEnvelopeHeader header,
        ExactRequest request)
    {
        var endpoint = GetTcpEndpoint();
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var routerSocket = context.CreateRouterSocket();
        await using var dealerSocket = context.CreateDealerSocket();
        routerSocket.Options.Linger = TimeSpan.Zero;
        dealerSocket.Options.Linger = TimeSpan.Zero;
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        var router = new ZLinkBackendRouterSocketWrapper(routerSocket);
        var reporter = new ZLinkDispatchErrorReporter(registration.DispatchOptions, logger);
        var dispatcher = new ZLinkRoutePacketDispatcher(
            "mesh",
            router,
            new ZLinkRouteHandlerRegistry([descriptor]),
            new ZLinkRouteHandlerInvoker(services, registration.Codecs),
            registration.Codecs,
            ZLinkNoRouteInternalPacketDispatcher.Instance,
            reporter,
            null,
            logger);
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, request, typeof(ExactRequest), null);
        var requestTask = dealerSocket.Request()
            .Message(parts[0])
            .Message(parts[1])
            .Timeout(TimeSpan.FromSeconds(2))
            .Async();
        ZLinkMessageParts.DisposeAll(parts);

        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchAsync(received, CancellationToken.None);
        return await requestTask.WaitAsync(TimeSpan.FromSeconds(2));
    }

    private static void AssertReceivedThenReplied(
        IReadOnlyList<string> lines,
        ZLinkDispatchErrorSurface surface)
    {
        var matching = lines
            .Where(line => line.Contains($"surface={surface}", StringComparison.Ordinal))
            .ToArray();
        Assert.Equal(2, matching.Length);
        Assert.Contains("phase=received", matching[0], StringComparison.Ordinal);
        Assert.Contains("phase=replied", matching[1], StringComparison.Ordinal);
        Assert.All(matching, line =>
        {
            Assert.Contains($"flow={ExactFlowId}", line, StringComparison.Ordinal);
            Assert.Contains($"corr={ExactCorrelationId}", line, StringComparison.Ordinal);
        });
    }

    private const string ExactFlowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d";
    private const string ExactCorrelationId = "dispatch-correlation-41";

    private sealed record ExactRequest(string Value);

    private sealed record ExactReply(string Value);

    private sealed class ExactChannelRequestHandler : IZLinkRequestHandler<ExactRequest, ExactReply>
    {
        public ValueTask<ExactReply> HandleAsync(
            ExactRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new ExactReply(request.Value.ToUpperInvariant()));
        }
    }

    private sealed class FailingExactChannelRequestHandler : IZLinkRequestHandler<ExactRequest, ExactReply>
    {
        public ValueTask<ExactReply> HandleAsync(
            ExactRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = request;
            _ = context;
            _ = cancellationToken;
            throw new InvalidOperationException("exact dispatch failure");
        }
    }

    private sealed class ExactRouteRequestHandler : IZLinkRouteRequestHandler<ExactRequest, ExactReply>
    {
        public ValueTask<ExactReply> HandleAsync(
            ExactRequest request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new ExactReply(request.Value.ToUpperInvariant()));
        }
    }
}
