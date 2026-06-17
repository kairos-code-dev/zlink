namespace Systems.Zlink.Stream.Connector.Contracts;

/// <summary>
/// Converts a business object to and from a <see cref="ZlinkStreamEncodedPayload"/>.
/// Set <see cref="ZlinkStreamConnectorOptions.PayloadCodec"/> to plug a custom codec
/// (for example Avro or Thrift) into the typed connector send/request/observe API.
/// When left unset, the connector uses its built-in protobuf/MessagePack/JSON
/// auto-codec selection.
/// </summary>
public interface IZlinkStreamPayloadCodec
{
    /// <summary>Encodes <paramref name="payload"/> into a wire payload.</summary>
    ZlinkStreamEncodedPayload Encode<TPayload>(TPayload payload);

    /// <summary>Decodes a wire payload back into an instance of <typeparamref name="TPayload"/>.</summary>
    TPayload Decode<TPayload>(ZlinkStreamEncodedPayload payload);
}
