using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkRuntimeEventDispatcher(IServiceScopeFactory scopeFactory) : IZLinkRuntimeEventPublisher
{
    public async ValueTask PublishAsync<TEvent>(
        TEvent @event,
        CancellationToken cancellationToken)
        where TEvent : IZLinkRuntimeEvent
    {
        await using var scope = scopeFactory.CreateAsyncScope();
        var handlers = scope.ServiceProvider.GetServices<IZLinkRuntimeEventHandler<TEvent>>();

        foreach (var handler in handlers) await handler.HandleAsync(@event, cancellationToken);
    }
}
