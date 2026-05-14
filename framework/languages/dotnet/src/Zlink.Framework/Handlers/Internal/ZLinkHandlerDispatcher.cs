using System.Buffers;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Handlers.Internal;

internal sealed class ZLinkHandlerDispatcher(
    IServiceScopeFactory scopeFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask<object?> DispatchAsync(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        await using var scope = scopeFactory.CreateAsyncScope();
        var scopedContext = RebindContext(context, scope.ServiceProvider);
        var invocation = new ZLinkHandlerInvocation(
            message,
            scopedContext,
            scopedContext.ChannelName,
            scopedContext.PacketName,
            scope.ServiceProvider);
        var pipeline = BuildPipeline(endpoint, message, scopedContext, invocation, scope.ServiceProvider);
        return await pipeline(cancellationToken).ConfigureAwait(false);
    }

    private ZLinkHandlerDelegate BuildPipeline(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        ZLinkHandlerInvocation invocation,
        IServiceProvider services)
    {
        ZLinkHandlerDelegate pipeline = ct => InvokeHandlerAsync(endpoint, message, context, services, ct);

        for (var index = registration.Filters.Count - 1; index >= 0; index--)
        {
            var next = pipeline;
            var filterType = registration.Filters[index];
            pipeline = async ct =>
            {
                var filter = (IZLinkHandlerFilter)services.GetRequiredService(filterType);
                return await filter.InvokeAsync(invocation, next, ct).ConfigureAwait(false);
            };
        }

        return pipeline;
    }

    private static async ValueTask<object?> InvokeHandlerAsync(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        IServiceProvider services,
        CancellationToken cancellationToken)
    {
        var handler = services.GetRequiredService(endpoint.DeclaringType);
        var args = ArrayPool<object?>.Shared.Rent(endpoint.ArgumentPlan.Count);
        try
        {
            BuildArguments(endpoint, message, context, cancellationToken, args);
            var result = endpoint.Invoker(handler, args);
            return await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
        }
        finally
        {
            Array.Clear(args, 0, endpoint.ArgumentPlan.Count);
            ArrayPool<object?>.Shared.Return(args);
        }
    }

    private static void BuildArguments(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken,
        object?[] args)
    {
        for (var i = 0; i < endpoint.ArgumentPlan.Count; i++)
        {
            switch (endpoint.ArgumentPlan[i])
            {
                case ZLinkHandlerArgumentKind.Message:
                    args[i] = message;
                    break;
                case ZLinkHandlerArgumentKind.Context:
                    args[i] = context;
                    break;
                case ZLinkHandlerArgumentKind.CancellationToken:
                    args[i] = cancellationToken;
                    break;
            }
        }
    }

    private static ZLinkHandlerContext RebindContext(ZLinkHandlerContext context, IServiceProvider services)
    {
        return context switch
        {
            ZLinkRequestContext request => new ZLinkRequestContext(
                request.ChannelName,
                request.PacketName,
                request.ContentType,
                request.CorrelationId,
                request.Deadline,
                services,
                request.ConnectionAborted),
            ZLinkSendContext send => new ZLinkSendContext(
                send.ChannelName,
                send.PacketName,
                send.ContentType,
                send.CorrelationId,
                send.Deadline,
                services,
                send.ConnectionAborted),
            ZLinkPublishContext publish => new ZLinkPublishContext(
                publish.ChannelName,
                publish.PacketName,
                publish.ContentType,
                publish.CorrelationId,
                publish.Deadline,
                publish.Topic,
                publish.Source,
                services,
                publish.ConnectionAborted),
            _ => throw new InvalidOperationException("Unknown handler context type."),
        };
    }
}
