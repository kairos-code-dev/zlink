using Systems.Zlink;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;

namespace SpotService.Shared;

/// <summary>
/// Resolve-once helper for the E2E servers: spot rid strings from test
/// payloads become spot full addresses before messaging, per the
/// spot-address messaging draft. A missing row fails typed so negative
/// scenarios observe SpotRouteNotFound without waiting out a timeout.
/// </summary>
public static class SpotAddressRouting
{
    public static async ValueTask<SpotHandle> ResolveRequiredAsync(
        this IZLinkSpotHandleResolver locator,
        string spotRid,
        CancellationToken cancellationToken = default)
    {
        return await locator.ResolveSpotHandleAsync(RoutingId.From(spotRid), cancellationToken)
               ?? throw new ZLinkFrameworkException(
                   ZLinkFrameworkErrorKind.SpotRouteNotFound,
                   $"Spot '{spotRid}' has no live address.");
    }
}
