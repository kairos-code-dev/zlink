using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Handlers;

internal static class ZLinkHandlerInvocationEngine
{
    public static ValueTask<object?> InvokeAsync(
        IServiceProvider services,
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        IReadOnlyList<ZLinkHandlerArgumentKind> argumentPlan,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        var handler = ActivatorUtilities.GetServiceOrCreateInstance(services, handlerType);
        return InvokeAsync(handler, invoker, argumentPlan, message, context, cancellationToken);
    }

    public static ValueTask<object?> InvokeAsync(
        object handler,
        ZLinkHandlerMethodInvoker invoker,
        IReadOnlyList<ZLinkHandlerArgumentKind> argumentPlan,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        object? arg0 = null;
        object? arg1 = null;
        object? arg2 = null;
        for (var i = 0; i < argumentPlan.Count; i++)
        {
            if (i >= 3)
                throw new InvalidOperationException("Handler methods support at most three arguments.");

            var value = argumentPlan[i] switch
            {
                ZLinkHandlerArgumentKind.Message => message,
                ZLinkHandlerArgumentKind.Context => context,
                ZLinkHandlerArgumentKind.CancellationToken => cancellationToken,
                _ => null
            };

            switch (i)
            {
                case 0:
                    arg0 = value;
                    break;
                case 1:
                    arg1 = value;
                    break;
                case 2:
                    arg2 = value;
                    break;
            }
        }

        var result = invoker(handler, arg0, arg1, arg2);
        return ZLinkHandlerResultAwaiter.AwaitAsync(result);
    }

    public static ValueTask<object?> InvokeAsync(
        object handler,
        ZLinkHandlerMethodInvoker invoker,
        int argumentCount,
        Action<ZLinkHandlerArgumentSink> configureArguments)
    {
        object? arg0 = null;
        object? arg1 = null;
        object? arg2 = null;
        switch (argumentCount)
        {
            case 0:
                break;
            case 1:
                configureArguments(new ZLinkHandlerArgumentSink(
                    value => arg0 = value,
                    null,
                    null));
                break;
            case 2:
                configureArguments(new ZLinkHandlerArgumentSink(
                    value => arg0 = value,
                    value => arg1 = value,
                    null));
                break;
            case 3:
                configureArguments(new ZLinkHandlerArgumentSink(
                    value => arg0 = value,
                    value => arg1 = value,
                    value => arg2 = value));
                break;
            default:
                throw new InvalidOperationException("Handler methods support at most three arguments.");
        }

        var result = invoker(handler, arg0, arg1, arg2);
        return ZLinkHandlerResultAwaiter.AwaitAsync(result);
    }
}

internal readonly struct ZLinkHandlerArgumentSink(
    Action<object?>? set0,
    Action<object?>? set1,
    Action<object?>? set2)
{
    public object? this[int index]
    {
        set
        {
            switch (index)
            {
                case 0:
                    set0?.Invoke(value);
                    break;
                case 1:
                    set1?.Invoke(value);
                    break;
                case 2:
                    set2?.Invoke(value);
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(index));
            }
        }
    }
}
