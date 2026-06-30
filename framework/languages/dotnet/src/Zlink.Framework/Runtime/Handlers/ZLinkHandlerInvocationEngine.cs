using System.Buffers;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Handlers;

internal static class ZLinkHandlerInvocationEngine
{
    public static async ValueTask<object?> InvokeAsync(
        IServiceProvider services,
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        int argumentCount,
        Action<object?[]> configureArguments)
    {
        var handler = ActivatorUtilities.GetServiceOrCreateInstance(services, handlerType);
        return await InvokeAsync(handler, invoker, argumentCount, configureArguments)
            .ConfigureAwait(false);
    }

    public static async ValueTask<object?> InvokeAsync(
        object handler,
        ZLinkHandlerMethodInvoker invoker,
        int argumentCount,
        Action<object?[]> configureArguments)
    {
        var arguments = ArrayPool<object?>.Shared.Rent(argumentCount);
        try
        {
            configureArguments(arguments);
            var result = invoker(handler, arguments);
            return await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
        }
        finally
        {
            Array.Clear(arguments, 0, argumentCount);
            ArrayPool<object?>.Shared.Return(arguments);
        }
    }
}