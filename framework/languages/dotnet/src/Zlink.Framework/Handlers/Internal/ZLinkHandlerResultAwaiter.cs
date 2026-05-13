using System.Collections.Concurrent;
using System.Reflection;

namespace Zlink.Framework.Handlers.Internal;

internal static class ZLinkHandlerResultAwaiter
{
    private static readonly ConcurrentDictionary<Type, Func<object, ValueTask<object?>>> GenericAwaiters = new();

    public static async ValueTask<object?> AwaitAsync(object? result)
    {
        if (result is null)
        {
            return null;
        }

        switch (result)
        {
            case Task task when result.GetType() == typeof(Task):
                await task.ConfigureAwait(false);
                return null;
            case ValueTask valueTask:
                await valueTask.ConfigureAwait(false);
                return null;
        }

        return IsGenericAwaitable(result.GetType())
            ? await GenericAwaiters.GetOrAdd(result.GetType(), CreateGenericAwaiter)(result).ConfigureAwait(false)
            : result;
    }

    private static bool IsGenericAwaitable(Type resultType)
    {
        return resultType.IsGenericType
            && (resultType.GetGenericTypeDefinition() == typeof(Task<>)
                || resultType.GetGenericTypeDefinition() == typeof(ValueTask<>));
    }

    private static Func<object, ValueTask<object?>> CreateGenericAwaiter(Type resultType)
    {
        var resultValueType = resultType.GetGenericArguments()[0];
        var methodName = resultType.GetGenericTypeDefinition() == typeof(Task<>)
            ? nameof(AwaitTaskAsync)
            : nameof(AwaitValueTaskAsync);
        var method = typeof(ZLinkHandlerResultAwaiter)
            .GetMethod(methodName, BindingFlags.Static | BindingFlags.NonPublic)!
            .MakeGenericMethod(resultValueType);
        return (Func<object, ValueTask<object?>>)method.CreateDelegate(typeof(Func<object, ValueTask<object?>>));
    }

    private static async ValueTask<object?> AwaitTaskAsync<T>(object result)
        => await ((Task<T>)result).ConfigureAwait(false);

    private static async ValueTask<object?> AwaitValueTaskAsync<T>(object result)
        => await ((ValueTask<T>)result).ConfigureAwait(false);
}
