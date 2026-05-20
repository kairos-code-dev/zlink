using Bingo.Shared.Configuration;
using Bingo.Server.Infrastructure;

namespace Bingo.Server.Play;

internal sealed class BingoRoomDirectory(
    IZLinkSpotManager spots,
    RegistryPlayRoutePublisher routes)
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string? _currentRoomId;
    private int _reservedSeats;

    public async ValueTask<string> AllocateAsync(
        string mode,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(mode, "four-player", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unsupported bingo mode. mode={mode}");
        }

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_currentRoomId is null || _reservedSeats >= BingoRoomSpot.RequiredPlayers)
            {
                var room = await spots.CreateAsync(SampleNames.RoomSpotType, cancellationToken)
                    .ConfigureAwait(false);
                await routes.BindSpotRouteAsync(room.SpotRid, cancellationToken).ConfigureAwait(false);
                _currentRoomId = room.SpotRid.ToHex();
                _reservedSeats = 0;
            }

            _reservedSeats++;
            return _currentRoomId;
        }
        finally
        {
            _gate.Release();
        }
    }
}
