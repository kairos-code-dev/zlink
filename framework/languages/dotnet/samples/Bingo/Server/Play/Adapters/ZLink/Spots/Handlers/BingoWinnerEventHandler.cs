using Bingo.Server.Play.Adapters.ZLink.Spots;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;

internal sealed class BingoWinnerEventHandler : IZLinkSpotSubscriptionHandler<BingoRoom, BingoWinnerEvent>
{
    public ValueTask HandleAsync(
        BingoRoom spot,
        BingoWinnerEvent message,
        CancellationToken cancellationToken)
    {
        return spot.AnnounceWinnerAsync(message, cancellationToken);
    }
}
