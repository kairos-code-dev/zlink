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
        IZLinkMessageContext context,
        CancellationToken cancellationToken)
    {
        await using var scope = scopeFactory.CreateAsyncScope();
        object? result = null;
        var pipeline = BuildPipeline(
            endpoint,
            message,
            context,
            scope.ServiceProvider,
            value => result = value,
            cancellationToken);
        await pipeline().ConfigureAwait(false);
        return result;
    }

    private ZLinkHandlerFilterNext BuildPipeline(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        IZLinkMessageContext context,
        IServiceProvider services,
        Action<object?> setResult,
        CancellationToken cancellationToken)
    {
        ZLinkHandlerFilterNext pipeline = async () =>
        {
            var result = await InvokeHandlerAsync(
                    endpoint,
                    message,
                    context,
                    _activator,
                    services,
                    cancellationToken)
                .ConfigureAwait(false);
            setResult(result);
        };

        for (var index = registration.Filters.Count - 1; index >= 0; index--)
        {
            var next = pipeline;
            var filterType = registration.Filters[index];
            pipeline = async () =>
            {
                var filter = (IZLinkHandlerFilter)services.GetRequiredService(filterType);
                await filter.InvokeAsync(context, next, cancellationToken).ConfigureAwait(false);
            };
        }

        return pipeline;
    }

    private static async ValueTask<object?> InvokeHandlerAsync(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        IZLinkMessageContext context,
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
