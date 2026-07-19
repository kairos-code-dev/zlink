using System.Reflection;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Handlers;

public sealed class HandlerContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkHandlerContext),
        typeof(IZLinkRequestHandler<,>),
        typeof(IZLinkSendHandler<>),
        typeof(IZLinkFanoutHandler<>),
        typeof(IZLinkHandlerFilter))]
    public async Task Channel_handlers_and_filters_keep_user_code_behind_typed_contracts()
    {
        var sendHandler = new PlayerJoinedSendHandler();
        var requestHandler = new AuthenticateRequestHandler();
        var publishHandler = new RoomEventPublishHandler();
        var filter = new AuditingFilter();

        await sendHandler.HandleAsync(new PlayerJoined("alice"), null!, CancellationToken.None);
        var reply = await requestHandler.HandleAsync(new Authenticate("alice"), null!, CancellationToken.None);
        await publishHandler.HandleAsync(new RoomEvent("started"), CancellationToken.None);

        await filter.InvokeAsync(
            null!,
            () => ValueTask.CompletedTask,
            CancellationToken.None);

        Assert.True(sendHandler.WasCalled);
        Assert.Equal("alice", reply.PlayerId);
        Assert.True(publishHandler.WasCalled);
    }

    [Fact]
    [ContractExample(typeof(IZLinkHandlerContext))]
    public void Handler_context_exposes_only_dispatch_metadata()
    {
        var publicProperties = typeof(IZLinkHandlerContext)
            .GetProperties()
            .Select(static property => property.Name)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            new[]
            {
                nameof(IZLinkHandlerContext.ChannelName),
                nameof(IZLinkHandlerContext.ConnectionAborted),
                nameof(IZLinkHandlerContext.ContentType),
                nameof(IZLinkHandlerContext.MeshName),
                nameof(IZLinkHandlerContext.Metadata),
                nameof(IZLinkHandlerContext.PacketName)
            },
            publicProperties);

        Assert.Null(
            typeof(ZLinkHandlerInvocation).GetProperty(
                "Services",
                BindingFlags.Instance | BindingFlags.NonPublic));
    }

    private sealed record Authenticate(string PlayerId);

    private sealed record Authenticated(string PlayerId);

    private sealed record PlayerJoined(string PlayerId);

    private sealed record RoomEvent(string State);

    private sealed class AuthenticateRequestHandler : IZLinkRequestHandler<Authenticate, Authenticated>
    {
        public ValueTask<Authenticated> HandleAsync(
            Authenticate request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new Authenticated(request.PlayerId));
        }
    }

    private sealed class PlayerJoinedSendHandler : IZLinkSendHandler<PlayerJoined>
    {
        public bool WasCalled { get; private set; }

        public ValueTask HandleAsync(
            PlayerJoined message,
            ZLinkSendContext context,
            CancellationToken cancellationToken)
        {
            WasCalled = true;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RoomEventPublishHandler : IZLinkFanoutHandler<RoomEvent>
    {
        public bool WasCalled { get; private set; }

        public ValueTask HandleAsync(
            RoomEvent message,
            CancellationToken cancellationToken)
        {
            WasCalled = true;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class AuditingFilter : IZLinkHandlerFilter
    {
        public ValueTask InvokeAsync(
            ZLinkHandlerInvocation invocation,
            ZLinkHandlerFilterNext next,
            CancellationToken cancellationToken)
        {
            return next();
        }
    }
}
