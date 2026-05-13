using System.Buffers.Binary;
using System.Collections.Concurrent;

using System.Net.Security;

using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Transport;

internal sealed class WebSocketConnection(ClientWebSocket webSocket) : IZlinkStreamConnection
{
    private readonly byte[] _receiveBuffer = new byte[8192];
    private byte[]? _pendingMessage;
    private int _pendingOffset;

    public bool CanWriteSegments => false;

    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        while (_pendingMessage is null)
        {
            using var message = new System.IO.MemoryStream();
            WebSocketReceiveResult result;
            do
            {
                result = await webSocket.ReceiveAsync(_receiveBuffer, cancellationToken).ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    return 0;
                }

                if (result.MessageType != WebSocketMessageType.Binary)
                {
                    throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "WebSocket text messages are not supported.");
                }

                message.Write(_receiveBuffer, 0, result.Count);
            }
            while (!result.EndOfMessage);

            if (message.Length == 0)
            {
                continue;
            }

            _pendingMessage = message.ToArray();
            _pendingOffset = 0;
        }

        var remaining = _pendingMessage.Length - _pendingOffset;
        var count = Math.Min(buffer.Length, remaining);
        _pendingMessage.AsMemory(_pendingOffset, count).CopyTo(buffer);
        _pendingOffset += count;
        if (_pendingOffset == _pendingMessage.Length)
        {
            _pendingMessage = null;
            _pendingOffset = 0;
        }

        return count;
    }

    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
        => await webSocket.SendAsync(buffer, WebSocketMessageType.Binary, true, cancellationToken).ConfigureAwait(false);

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        if (webSocket.State is WebSocketState.Open or WebSocketState.CloseReceived)
        {
            await webSocket.CloseAsync(WebSocketCloseStatus.NormalClosure, "closed", cancellationToken).ConfigureAwait(false);
        }

        webSocket.Dispose();
    }
}
