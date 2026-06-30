namespace Zlink.Framework.Runtime.Handlers;

internal static class ZLinkHandlerContractInspector
{
    public static bool TryFindGenericInterface(
        Type handlerType,
        Type genericDefinition,
        out Type[] arguments)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != genericDefinition)
                continue;

            arguments = implemented.GetGenericArguments();
            return true;
        }

        arguments = [];
        return false;
    }

    public static IEnumerable<(Type Definition, Type[] Arguments)> EnumerateGenericInterfaces(
        Type handlerType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
            if (implemented.IsGenericType)
                yield return (implemented.GetGenericTypeDefinition(), implemented.GetGenericArguments());
    }
}