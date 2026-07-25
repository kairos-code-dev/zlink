using System.Text.Json;
using ObservabilityOps.Server.Play.Domain;
using Zlink.Framework.Contracts.Actors;

namespace ObservabilityOps.Server.Play.Infrastructure;

internal sealed class PlayerActor(
    Player player,
    IZLinkActorContext context) : IZLinkActor
{
    public IZLinkActorContext Context { get; } = context;

    public Player Player { get; } = player;
}

internal sealed record PlayerRelocationState(string RoomRid);

internal sealed class PlayerActorFactory : IZLinkActorFactory<PlayerActor>
{
    public ValueTask<PlayerActor> CreateAsync(
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(
            new PlayerActor(new Player(context.ActorId), context));
    }
}

internal sealed class PlayerActorRelocationAdapter
    : IZLinkActorRelocationAdapter<PlayerActor>
{
    public ValueTask<byte[]> CaptureAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(JsonSerializer.SerializeToUtf8Bytes(
            new PlayerRelocationState(actor.Player.RoomRid)));
    }

    public ValueTask RestoreAsync(
        PlayerActor actor,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var restored = JsonSerializer.Deserialize<PlayerRelocationState>(
            payload.Span) ?? throw new InvalidDataException(
            "Player relocation state is empty.");
        if (!string.IsNullOrWhiteSpace(restored.RoomRid))
            actor.Player.JoinRoom(restored.RoomRid);
        return ValueTask.CompletedTask;
    }
}
