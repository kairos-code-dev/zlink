using Bingo.Server.Play.BingoRoomSpots;
using Bingo.Shared.Configuration;

namespace Bingo.Server.Play;

internal sealed class BingoRoomDirectory(IZLinkSpotManager spots)
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string? _currentRoomId;
    private BingoRoomSettings? _currentRoomSettings;
    private int _reservedSeats;
    private int _roomSeq;

    public async ValueTask<string> AllocateAsync(
        string mode,
        CancellationToken cancellationToken)
    {
        var settings = BingoRoomSettings.Create(mode, _roomSeq + 1);

        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_currentRoomId is null
                || _currentRoomSettings is null
                || !string.Equals(_currentRoomSettings.Mode, settings.Mode, StringComparison.Ordinal)
                || _reservedSeats >= _currentRoomSettings.RequiredPlayers)
            {
                settings = settings with { RoomName = $"Bingo Room {++_roomSeq:000}" };
                using var settingsPart = settings.ToJson();
                var room = await spots.CreateAsync(SampleNames.RoomSpotType, [settingsPart], cancellationToken)
                    ;
                _currentRoomId = room.SpotRid.ToHex();
                _currentRoomSettings = settings;
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
