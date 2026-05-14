using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;
using Xunit;


public sealed partial class StreamConnectorTests
{
    private static async Task<(byte[] Header, byte[] Body)> ReadPacketAsync(global::System.IO.Stream stream)
    {
        var prefix = new byte[6];
        await stream.ReadExactlyAsync(prefix);
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(prefix.AsSpan(0, 2));
        var bodySize = BinaryPrimitives.ReadUInt32BigEndian(prefix.AsSpan(2, 4));
        var header = new byte[headerSize];
        var body = new byte[bodySize];
        await stream.ReadExactlyAsync(header);
        await stream.ReadExactlyAsync(body);
        return (header, body);
    }

    private static async Task WritePacketAsync(global::System.IO.Stream stream, byte[] header, byte[] body)
    {
        var prefix = new byte[6];
        BinaryPrimitives.WriteUInt16BigEndian(prefix.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(prefix.AsSpan(2, 4), (uint)body.Length);
        await stream.WriteAsync(prefix);
        await stream.WriteAsync(header);
        await stream.WriteAsync(body);
    }

    private static byte[] EncodePacket(byte[] header, byte[] body)
    {
        var frame = new byte[6 + header.Length + body.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4), (uint)body.Length);
        header.CopyTo(frame.AsSpan(6));
        body.CopyTo(frame.AsSpan(6 + header.Length));
        return frame;
    }

    private static (byte[] Header, byte[] Body) DecodePacket(byte[] frame)
    {
        var headerSize = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(0, 2));
        var bodySize = BinaryPrimitives.ReadUInt32BigEndian(frame.AsSpan(2, 4));
        var header = frame.AsSpan(6, headerSize).ToArray();
        var body = frame.AsSpan(6 + headerSize, (int)bodySize).ToArray();
        return (header, body);
    }

    private static async Task<byte[]> ReceiveWebSocketMessageAsync(WebSocket webSocket)
    {
        var buffer = new byte[4096];
        using var stream = new MemoryStream();
        WebSocketReceiveResult result;
        do
        {
            result = await webSocket.ReceiveAsync(buffer, CancellationToken.None);
            stream.Write(buffer, 0, result.Count);
        }
        while (!result.EndOfMessage);

        return stream.ToArray();
    }

    private static int GetFreeTcpPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private static X509Certificate2 CreateSelfSignedCertificate()
    {
        using var rsa = RSA.Create(2048);
        var request = new CertificateRequest(
            "CN=localhost",
            rsa,
            HashAlgorithmName.SHA256,
            RSASignaturePadding.Pkcs1);
        request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, false));
        request.CertificateExtensions.Add(new X509KeyUsageExtension(
            X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment,
            false));
        var certificate = request.CreateSelfSigned(
            DateTimeOffset.UtcNow.AddMinutes(-1),
            DateTimeOffset.UtcNow.AddDays(1));
        return new X509Certificate2(certificate.Export(X509ContentType.Pfx));
    }

    private sealed record Ping(string Text);

    private sealed record Pong(string Text);

    [MessagePackObject]
    public sealed class PackedPing
    {
        [Key(0)]
        public string Text { get; set; } = string.Empty;
    }

    [ZlinkStreamPacketName("custom.packet")]
    private sealed record NamedPacket(string Text);

}
