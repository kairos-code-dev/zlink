using System.Buffers.Binary;
using System.Text;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamSessionClosingCodec
{
    public const string ControlName = "session-closing";
    private const byte Version = 1;
    private const byte ServerDrainReason = 4;
    private const int MaximumDiagnosticBytes = 512;
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);

    public static byte[] EncodeServerDrain(string? diagnostic = null)
    {
        var length = diagnostic is null ? 0 : StrictUtf8.GetByteCount(diagnostic);
        if (length > MaximumDiagnosticBytes)
            throw new ArgumentOutOfRangeException(
                nameof(diagnostic),
                "Session-closing diagnostic must not exceed 512 UTF-8 bytes.");

        var payload = new byte[4 + length];
        payload[0] = Version;
        payload[1] = ServerDrainReason;
        BinaryPrimitives.WriteUInt16BigEndian(payload.AsSpan(2, 2), (ushort)length);
        if (length > 0) StrictUtf8.GetBytes(diagnostic!, payload.AsSpan(4));
        return payload;
    }

    public static ZlinkStreamHeader CreateHeader() => new(
        ZlinkStreamMessageKind.Control,
        ZlinkStreamCodec.Raw,
        ZlinkStreamHeaderFlags.None,
        null,
        ControlName,
        ZlinkStreamMetadata.Empty);
}
