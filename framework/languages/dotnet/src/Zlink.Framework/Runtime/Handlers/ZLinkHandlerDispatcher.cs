using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Handlers;

internal sealed class ZLinkHandlerDispatcher(
    IServiceScopeFactory scopeFactory,
    ZLinkFrameworkRegistration registration)
{
    private readonly ZLinkHandlerActivator _activator = new();

    public async ValueTask<object?> DispatchAsync(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        await using var scope = scopeFactory.CreateAsyncScope();
        var invocation = new ZLinkHandlerInvocation(
            message,
            context);
        var pipeline = BuildPipeline(endpoint, message, context, invocation, scope.ServiceProvider);
        return await pipeline(cancellationToken).ConfigureAwait(false);
    }

    private ZLinkHandlerDelegate BuildPipeline(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        ZLinkHandlerInvocation invocation,
        IServiceProvider services)
    {
        ZLinkHandlerDelegate pipeline = ct => InvokeHandlerAsync(endpoint, message, context, _activator, services, ct);

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
        ZLinkHandlerActivator activator,
        IServiceProvider services,
        CancellationToken cancellationToken)
    {
        var handler = activator.Create(services, endpoint.DeclaringType);
        return await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                endpoint.Invoker,
                endpoint.ArgumentPlan,
                message,
                context,
                cancellationToken)
            .ConfigureAwait(false);
    }

}
