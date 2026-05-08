namespace Zlink.Framework.Handlers.Internal;

internal static class ZLinkHandlerResultAwaiter
{
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

        var resultType = result.GetType();
        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(Task<>))
        {
            await ((Task)result).ConfigureAwait(false);
            return ((dynamic)result).Result;
        }

        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(ValueTask<>))
        {
            var asTask = (Task)((dynamic)result).AsTask();
            await asTask.ConfigureAwait(false);
            return ((dynamic)asTask).Result;
        }

        return result;
    }
}
