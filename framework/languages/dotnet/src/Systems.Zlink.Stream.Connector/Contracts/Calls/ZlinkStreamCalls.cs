namespace Systems.Zlink.Stream.Connector.Contracts.Calls;

public interface IZlinkStreamSendCall
{
    IZlinkStreamSendCall PacketName(string name);

    IZlinkStreamSendCall Metadata(string key, string value);

    IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata);

    IZlinkStreamSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZlinkStreamRequestCall
{
    IZlinkStreamRequestCall PacketName(string name);

    IZlinkStreamRequestCall Metadata(string key, string value);

    IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata);

    IZlinkStreamRequestCall Timeout(TimeSpan timeout);

    IZlinkStreamRequestCall Compress();

    ValueTask<ZlinkStreamEncodedBody> SubmitAsync(CancellationToken cancellationToken = default);

    void Submit(Action<ZlinkStreamResult> callback);

    void Submit(Action<ZlinkStreamResult<ZlinkStreamEncodedBody>> callback);
}
