using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotAmbientContext
{
    private static readonly AsyncLocal<ZLinkSpotActivation?> Current = new();

    public static ZLinkSpotActivation? CurrentOrDefault => Current.Value;

    public static ZLinkSpotActivation RequireCurrent()
    {
        return Current.Value
            ?? throw new InvalidOperationException(
                "IZLinkSpotClient can only be used inside an active SPOT callback.");
    }

    public static IDisposable Push(ZLinkSpotActivation activation)
    {
        var previous = Current.Value;
        Current.Value = activation;
        return new Revert(previous);
    }

    private sealed class Revert(ZLinkSpotActivation? previous) : IDisposable
    {
        public void Dispose()
        {
            Current.Value = previous;
        }
    }
}
