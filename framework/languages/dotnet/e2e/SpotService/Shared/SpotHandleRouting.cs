using Systems.Zlink;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;

namespace SpotService.Shared;

/// <summary>
/// Resolves the spot id from an E2E payload once and returns the opaque messaging
/// handle. A missing live location row fails with the typed routing error so the
/// negative scenarios do not wait for a request timeout.
/// </summary>
public static class SpotAddressRouting
{
    public static async ValueTask<SpotHandle> ResolveRequiredAsync(
        this IZLinkSpotHandleResolver locator,
        string meshName,
        string spotRid,
        CancellationToken cancellationToken = default)
    {
        return await locator.ResolveSpotHandleAsync(
                   meshName,
                   RoutingId.From(spotRid),
                   cancellationToken)
               ?? throw new ZLinkFrameworkException(
                   ZLinkFrameworkErrorKind.SpotRouteNotFound,
                   $"Spot '{spotRid}' has no live location row in RouteMesh '{meshName}'.");
    }

    public static ValueTask<SpotHandle> ResolveRequiredAsync(
        this IZLinkSpotHandleResolver locator,
        string spotRid,
        CancellationToken cancellationToken = default) =>
        locator.ResolveRequiredAsync(SpotServiceNames.SpotChannel, spotRid, cancellationToken);
}
