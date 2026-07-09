using System.Collections.Concurrent;
using System.Linq.Expressions;
using System.Reflection;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Handlers;

internal sealed class ZLinkHandlerDispatcher(
    IServiceScopeFactory scopeFactory,
    ZLinkFrameworkRegistration registration)
{
    private static readonly ConcurrentDictionary<Type, Func<object>?> ParameterlessFactories = new();

    public ValueTask<object?> DispatchAsync(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        if (registration.Filters.Count == 0
            && TryCreateParameterlessHandler(endpoint.DeclaringType, out var handler))
        {
            return ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                endpoint.Invoker,
                endpoint.ArgumentPlan,
                message,
                context,
                cancellationToken);
        }

        return DispatchWithPipelineAsync(endpoint, message, context, cancellationToken);
    }

    private async ValueTask<object?> DispatchWithPipelineAsync(
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

    private static bool TryCreateParameterlessHandler(Type handlerType, out object handler)
    {
        var factory = ParameterlessFactories.GetOrAdd(
            handlerType,
            static type =>
            {
                var constructor = type.GetConstructor(
                    BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                    binder: null,
                    Type.EmptyTypes,
                    modifiers: null);
                if (constructor is null) return null;

                var create = Expression.New(constructor);
                var convert = Expression.Convert(create, typeof(object));
                return Expression.Lambda<Func<object>>(convert).Compile();
            });
        if (factory is null)
        {
            handler = null!;
            return false;
        }

        handler = factory();
        return true;
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
