using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;


public abstract partial class RegistrationValidationSupport
{
    protected sealed class TestFilter : IZLinkHandlerFilter
    {
        public ValueTask<object?> InvokeAsync(
            ZLinkHandlerInvocation invocation,
            ZLinkHandlerDelegate next,
            CancellationToken cancellationToken)
        {
            return next(cancellationToken);
        }
    }

    protected sealed record TestChannelRequest(string Value);

    protected sealed record TestChannelReply(string Value);

    [ZLinkHandlerGroup("validation-request")]
    protected sealed class TestChannelRequestHandler
        : IZLinkRequestHandler<TestChannelRequest, TestChannelReply>
    {
        public ValueTask<TestChannelReply> HandleAsync(
            TestChannelRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new TestChannelReply(request.Value));
        }
    }

    protected sealed class AlternateTestChannelRequestHandler
        : IZLinkRequestHandler<TestChannelRequest, TestChannelReply>
    {
        public ValueTask<TestChannelReply> HandleAsync(
            TestChannelRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new TestChannelReply(request.Value));
        }
    }

    protected sealed record TestSendMessage(string Value);

    protected sealed class AlternateTestSendHandler : IZLinkSendHandler<TestSendMessage>
    {
        public ValueTask HandleAsync(
            TestSendMessage message,
            ZLinkSendContext context,
            CancellationToken cancellationToken)
        {
            _ = message;
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    protected sealed record TestPublishedEvent(string Value);

    [ZLinkHandlerGroup("validation-publish")]
    protected sealed class TestPublishHandler : IZLinkPublishHandler<TestPublishedEvent>
    {
        public ValueTask HandleAsync(
            TestPublishedEvent message,
            ZLinkPublishContext context,
            CancellationToken cancellationToken)
        {
            _ = message;
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    protected sealed record TestRouteRequest(string Value);

    protected sealed record TestRouteReply(string Value);

    [ZLinkHandlerGroup("validation-route")]
    protected sealed class TestRouteRequestHandler
        : IZLinkRouteRequestHandler<TestRouteRequest, TestRouteReply>
    {
        public ValueTask<TestRouteReply> HandleAsync(
            TestRouteRequest request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new TestRouteReply(request.Value));
        }
    }

    protected sealed class AlternateTestRouteRequestHandler
        : IZLinkRouteRequestHandler<TestRouteRequest, TestRouteReply>
    {
        public ValueTask<TestRouteReply> HandleAsync(
            TestRouteRequest request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new TestRouteReply(request.Value));
        }
    }

    protected sealed class TestHeaderSession : IZLinkSession
    {
        public TestHeaderSession(IZLinkSessionContext context)
        {
            Context = context;
        }

        public IZLinkSessionContext Context { get; }

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message body,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    protected interface ITestSessionDependencyHandler
    {
    }

    protected sealed class TestSessionDependencyHandler : ITestSessionDependencyHandler
    {
    }

    protected sealed class TestSessionWithEnumerableHandlers(
        IZLinkSessionContext context,
        IEnumerable<ITestSessionDependencyHandler> handlers) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public IReadOnlyCollection<ITestSessionDependencyHandler> Handlers { get; } = handlers.ToArray();

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message body,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    protected sealed class TestSessionPacketContext
    {
        public int HandledCount { get; private set; }

        public void MarkHandled()
        {
            HandledCount++;
        }
    }

    protected sealed class TestSessionPacketHandler : IZLinkSessionPacketHandler<TestSessionPacketContext>
    {
        public string PacketName => "test.session.packet";

        public ValueTask HandleAsync(
            TestSessionPacketContext context,
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message payload,
            CancellationToken cancellationToken)
        {
            _ = header;
            _ = payload;
            cancellationToken.ThrowIfCancellationRequested();
            context.MarkHandled();
            return ValueTask.CompletedTask;
        }
    }

    protected sealed class TestSessionWithPacketDispatcher(
        IZLinkSessionContext context,
        IZLinkSessionPacketDispatcher<TestSessionPacketContext> dispatcher) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public IZLinkSessionPacketDispatcher<TestSessionPacketContext> Dispatcher { get; } = dispatcher;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message body,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    protected sealed class DuplicateSessionPacketContext;

    protected sealed class DuplicateSessionPacketHandler : IZLinkSessionPacketHandler<DuplicateSessionPacketContext>
    {
        public string PacketName => "duplicate.session.packet";

        public ValueTask HandleAsync(
            DuplicateSessionPacketContext context,
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message payload,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    protected sealed class SecondDuplicateSessionPacketHandler : IZLinkSessionPacketHandler<DuplicateSessionPacketContext>
    {
        public string PacketName => "duplicate.session.packet";

        public ValueTask HandleAsync(
            DuplicateSessionPacketContext context,
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message payload,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    protected sealed class TestSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    protected sealed class TestEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;
    }

    protected sealed class TestActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId, context));
        }
    }

    protected sealed class TestSpotRemoteAddressResolver : IZLinkSpotRemoteAddressResolver
    {
        public ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
            global::Systems.Zlink.RoutingId spotRid,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new ZLinkSpotRemoteAddress(
                "spots",
                global::Systems.Zlink.RoutingId.From("03"),
                spotRid,
                ZLinkSpotKind.User));
        }
    }

    protected sealed class TestActor(
        string actorId,
        IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }
}
