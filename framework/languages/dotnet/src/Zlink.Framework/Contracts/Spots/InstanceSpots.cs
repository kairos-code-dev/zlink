namespace Zlink.Framework.Contracts.Spots;

/// <summary>
///     A stable logical address whose first direct call may activate an
///     actor-free Instance Spot.
/// </summary>
public sealed record InstanceSpotAddress(
    string MeshName,
    string InstanceSpotType,
    RoutingId SpotRid);

/// <summary>Per-MeshNode limits for one registered Instance Spot type.</summary>
public sealed record ZLinkInstanceSpotFactoryOptions
{
    public int MaxActiveInstances { get; init; } = 4096;
    public TimeSpan ActivationTimeout { get; init; } = TimeSpan.FromSeconds(3);
}

/// <summary>
///     Actor-free lifecycle for a Spot activated through an
///     <see cref="InstanceSpotAddress" />.
/// </summary>
public interface IZLinkInstanceSpot
{
    IZLinkInstanceSpotContext Context { get; }

    void Configure()
    {
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

/// <summary>
///     Context available to one activated Instance Spot. It intentionally has
///     no Actor membership operations.
/// </summary>
public interface IZLinkInstanceSpotContext : IZLinkSpotCommonContext
{
    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);
}
