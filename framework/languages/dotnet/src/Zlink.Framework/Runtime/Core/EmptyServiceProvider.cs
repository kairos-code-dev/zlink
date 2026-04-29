using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

internal sealed class EmptyServiceProvider : IServiceProvider
{
    public static readonly EmptyServiceProvider Instance = new();

    public object? GetService(Type serviceType)
    {
        _ = serviceType;
        return null;
    }
}
