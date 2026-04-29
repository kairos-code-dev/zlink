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

internal sealed class WebSocketConnection : IZlinkStreamConnection
{
    private readonly ClientWebSocket _webSocket;
    private readonly Queue<byte> _pendingBytes = new();

    public WebSocketConnection(ClientWebSocket webSocket)
    {
        _webSocket = webSocket;
    }

    public bool CanWriteSegments => false;

    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        while (_pendingBytes.Count == 0)
        {
            var temp = new byte[8192];
            using var message = new System.IO.MemoryStream();
            WebSocketReceiveResult result;
            do
            {
                result = await _webSocket.ReceiveAsync(temp, cancellationToken).ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    return 0;
                }

                if (result.MessageType != WebSocketMessageType.Binary)
                {
                    throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "WebSocket text messages are not supported.");
                }

                message.Write(temp, 0, result.Count);
            }
            while (!result.EndOfMessage);

            foreach (var value in message.ToArray())
            {
                _pendingBytes.Enqueue(value);
            }
        }

        var count = Math.Min(buffer.Length, _pendingBytes.Count);
        var output = new byte[count];
        for (var i = 0; i < count; i++)
        {
            output[i] = _pendingBytes.Dequeue();
        }

        output.AsMemory().CopyTo(buffer);
        return count;
    }

    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
        => await _webSocket.SendAsync(buffer, WebSocketMessageType.Binary, true, cancellationToken).ConfigureAwait(false);

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        if (_webSocket.State is WebSocketState.Open or WebSocketState.CloseReceived)
        {
            await _webSocket.CloseAsync(WebSocketCloseStatus.NormalClosure, "closed", cancellationToken).ConfigureAwait(false);
        }

        _webSocket.Dispose();
    }
}
