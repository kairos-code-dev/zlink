namespace Systems.Zlink.Stream.Connector.Contracts;

/// <summary>
/// Represents a client-side stream connection to a ZLink stream endpoint.
/// </summary>
/// <remarks>
/// <see cref="On"/> is the normal callback path for long-lived push handling.
/// <see cref="WaitFor"/> is a
/// deterministic wait path for samples, command-line flows, and e2e scenario
/// tests.
/// </remarks>
public interface IZlinkStreamConnector : IAsyncDisposable
{
    /// <summary>
    /// Raised when the endpoint sends an error message.
    /// </summary>
    event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

    /// <summary>
    /// Raised after the connector becomes disconnected.
    /// </summary>
    event Func<CancellationToken, ValueTask>? Disconnected;

    /// <summary>
    /// Raised when the connection state changes.
    /// </summary>
    event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged;

    /// <summary>
    /// Gets whether the connector is currently connected.
    /// </summary>
    bool IsConnected { get; }

    /// <summary>
    /// Gets the current connection state.
    /// </summary>
    ZlinkStreamConnectionState State { get; }

    /// <summary>
    /// Gets the options used by this connector.
    /// </summary>
    ZlinkStreamConnectorOptions Options { get; }

    /// <summary>
    /// Gets the number of messages waiting for manual dispatch.
    /// </summary>
    int PendingDispatchCount { get; }

    /// <summary>
    /// Gets the number of received messages with <paramref name="name"/>.
    /// </summary>
    /// <remarks>
    /// The count includes messages that may already have been handled by a
    /// callback or consumed by a wait operation, so it is intended for
    /// diagnostics and scenario assertions rather than production flow control.
    /// </remarks>
    int ReceivedCount(string name);

    /// <summary>
    /// Starts the stream connection lifecycle operation.
    /// </summary>
    IZlinkStreamLifecycleCall Connect { get; }

    /// <summary>
    /// Starts the stream close lifecycle operation.
    /// </summary>
    IZlinkStreamLifecycleCall Close { get; }

    /// <summary>
    /// Starts a manual dispatch lifecycle operation.
    /// </summary>
    IZlinkStreamLifecycleCall Dispatch { get; }

    /// <summary>
    /// Starts a send operation for an encoded payload.
    /// </summary>
    IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload);

    /// <summary>
    /// Starts a request operation for an encoded payload.
    /// </summary>
    IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload);

    /// <summary>
    /// Registers a callback for messages with the given packet name.
    /// </summary>
    /// <remarks>
    /// The returned registration removes the callback when disposed. This is the
    /// normal API for production push handling.
    /// </remarks>
    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler);

    /// <summary>
    /// Starts a wait operation for the next unread received message with the given packet name.
    /// </summary>
    /// <remarks>
    /// The matched message is consumed by this wait operation. Use this API for
    /// deterministic sample, CLI, or e2e scenario flow; production clients
    /// should normally use <see cref="On"/>.
    /// </remarks>
    IZlinkStreamWaitCall WaitFor(string name);
}
