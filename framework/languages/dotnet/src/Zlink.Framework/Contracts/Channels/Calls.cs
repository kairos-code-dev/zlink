namespace Zlink.Framework.Contracts.Channels;

using Zlink.Framework.Contracts.Streams;

/// <summary>
///     Shared metadata builder of every send/request/publish call
///     (05-route-mesh §6). The last value set for a key wins; the encoded
///     frame is capped at 1024 bytes at submit time.
/// </summary>
public interface IZLinkMetadataCall<TSelf>
{
    TSelf Metadata(string key, string value);

    TSelf Metadata(ZLinkMessageMetadata metadata);
}

public interface IZLinkSendCall : IZLinkMetadataCall<IZLinkSendCall>
{
    ZLinkSubmitResult TrySubmit();

    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall : IZLinkMetadataCall<IZLinkRequestCall>
{
    IZLinkRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);

    ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall : IZLinkMetadataCall<IZLinkPublishCall>
{
    ZLinkPublishResult TrySubmit();

    ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public enum ZLinkSubmitStatus
{
    Submitted = 0,
    Backpressured = 1,
    TimedOut = 2,
    TargetNotFound = 3,
    RouteNotConnected = 4,
    Shutdown = 5
}

public readonly record struct ZLinkSubmitResult(ZLinkSubmitStatus Status);

public readonly record struct ZLinkLogicalMulticastDetail(
    ulong SnapshotRemoteNodeCount,
    ulong AdmittedRemoteNodeCount,
    ulong DroppedRemoteNodeCount,
    ulong SnapshotLocalSpotCount,
    ulong AdmittedLocalSpotCount,
    ulong DroppedLocalSpotCount);

public readonly record struct ZLinkPublishResult(
    ZLinkSubmitStatus Status,
    ZLinkLogicalMulticastDetail Detail);
