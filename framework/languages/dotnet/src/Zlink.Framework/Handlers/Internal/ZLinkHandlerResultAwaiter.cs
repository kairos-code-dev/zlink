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
            var task = (Task)result;
            await task.ConfigureAwait(false);
            return resultType.GetProperty("Result")!.GetValue(result);
        }

        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(ValueTask<>))
        {
            var asTask = (Task)resultType.GetMethod("AsTask")!.Invoke(result, null)!;
            await asTask.ConfigureAwait(false);
            return asTask.GetType().GetProperty("Result")!.GetValue(asTask);
        }

        return result;
    }
}
