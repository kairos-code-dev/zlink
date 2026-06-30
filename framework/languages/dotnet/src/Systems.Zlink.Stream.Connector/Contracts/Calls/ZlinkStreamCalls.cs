namespace Systems.Zlink.Stream.Connector.Contracts.Calls;

public interface IZlinkStreamLifecycleCall
{
    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZlinkStreamSendCall
{
    IZlinkStreamSendCall PacketName(string name);

    IZlinkStreamSendCall Metadata(string key, string value);

    IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata);

    IZlinkStreamSendCall Compress();

    ValueTask Async(CancellationToken cancellationToken = default);

    void Submit(CancellationToken cancellationToken = default)
    {
        _ = Async(cancellationToken).AsTask();
    }
}

public interface IZlinkStreamRequestCall
{
    IZlinkStreamRequestCall PacketName(string name);

    IZlinkStreamRequestCall Metadata(string key, string value);

    IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata);

    IZlinkStreamRequestCall Timeout(TimeSpan timeout);

    IZlinkStreamRequestCall Compress();

    ValueTask<ZlinkStreamEncodedPayload> Async(CancellationToken cancellationToken = default);

    void Submit(Action<ZlinkStreamResult> callback);

    void Submit(Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback);
}

public interface IZlinkStreamWaitCall
{
    IZlinkStreamWaitCall Timeout(TimeSpan timeout);

    IZlinkStreamWaitCall Where(Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate);

    ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> Async(
        CancellationToken cancellationToken = default);
}
