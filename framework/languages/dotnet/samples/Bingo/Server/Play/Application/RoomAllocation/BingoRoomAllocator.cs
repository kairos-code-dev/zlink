using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Adapters.ZLink.Spots;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Application.RoomAllocation;

internal sealed class BingoRoomAllocator(IZLinkSpotManager spots)
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private string? _currentRoomId;
    private BingoRoomSettings? _currentRoomSettings;
    private readonly Dictionary<string, string> _roomsByActor = new(StringComparer.Ordinal);
    private int _reservedSeats;
    private int _roomSeq;

    public async ValueTask<string> AllocateAsync(
        string mode,
        string actorId,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id is required for room allocation.");
        }

        var settings = BingoRoomSettings.Create(mode, _roomSeq + 1);

        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_roomsByActor.TryGetValue(actorId, out var existingRoomId))
            {
                return existingRoomId;
            }

            if (_currentRoomId is null
                || _currentRoomSettings is null
                || !string.Equals(_currentRoomSettings.Mode, settings.Mode, StringComparison.Ordinal)
                || _reservedSeats >= _currentRoomSettings.RequiredPlayers)
            {
                settings = settings with { RoomName = $"Bingo Room {++_roomSeq:000}" };
                using var settingsPart = ToPayload(settings).ToProto();
                var room = await spots.CreateAsync<BingoRoom>(settingsPart, cancellationToken);
                if (room.State != ZLinkSpotCreateState.Created)
                {
                    throw new InvalidOperationException("Bingo room creation was rejected.");
                }

                _currentRoomId = room.SpotRid.ToHex();
                _currentRoomSettings = settings;
                _reservedSeats = 0;
            }

            _reservedSeats++;
            _roomsByActor[actorId] = _currentRoomId;
            return _currentRoomId;
        }
        finally
        {
            _gate.Release();
        }
    }

    private static BingoRoomSettingsPayload ToPayload(BingoRoomSettings settings)
    {
        return new BingoRoomSettingsPayload
        {
            RoomName = settings.RoomName,
            Mode = settings.Mode,
            RequiredPlayers = settings.RequiredPlayers,
            MaxDrawNumber = settings.MaxDrawNumber,
        };
    }
}
