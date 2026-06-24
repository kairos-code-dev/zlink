using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Tracking;

internal sealed class CustomerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<CustomerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = actor;
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }
}
