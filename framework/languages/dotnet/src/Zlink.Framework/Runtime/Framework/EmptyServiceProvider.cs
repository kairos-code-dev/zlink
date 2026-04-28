using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime.Framework;

internal sealed class EmptyServiceProvider : IServiceProvider
{
    public static readonly EmptyServiceProvider Instance = new();

    public object? GetService(Type serviceType)
    {
        _ = serviceType;
        return null;
    }
}
