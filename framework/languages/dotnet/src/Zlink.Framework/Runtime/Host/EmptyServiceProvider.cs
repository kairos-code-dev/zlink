using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Host;

internal sealed class EmptyServiceProvider : IServiceProvider
{
    public static readonly EmptyServiceProvider Instance = new();

    public object? GetService(Type serviceType)
    {
        _ = serviceType;
        return null;
    }
}
