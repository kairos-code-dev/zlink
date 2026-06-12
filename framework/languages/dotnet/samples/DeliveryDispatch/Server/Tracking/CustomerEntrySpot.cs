using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Tracking;

internal sealed class CustomerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;
}
