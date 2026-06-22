using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Spots.Handlers;

internal sealed class BingoRewardAcquiredEventHandler : IZLinkSpotSubscriptionHandler<BingoRoom, BingoRewardAcquiredEvent>
{
    public ValueTask HandleAsync(
        BingoRoom spot,
        BingoRewardAcquiredEvent message,
        CancellationToken cancellationToken)
    {
        return spot.AnnounceRewardAsync(message, cancellationToken);
    }
}
