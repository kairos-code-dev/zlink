using MessagePack;
using Zlink.Framework.Contracts.Handlers;

namespace RegistrationCodec.Shared;

public static class RegistrationCodecNames
{
    public const string Channel = "reg-codec";
}

public sealed record EchoReq(string Value);

public sealed record EchoRes(string Value, string ContentType);

public sealed record EchoMsg(string CommandId, string Value);

[ZLinkPacket("EchoAuto")]
public sealed record EchoAutoReq(string Value);

[ZLinkPacket("EchoAutoMsg")]
public sealed record EchoAutoMsg(string CommandId, string Value);

[ZLinkPacket("EchoManual")]
public sealed record EchoManualReq(string Value);

[ZLinkPacket("EchoManualMsg")]
public sealed record EchoManualMsg(string CommandId, string Value);

public sealed record JsonEchoReq(string Value);

public sealed record JsonEchoMsg(string CommandId, string Value);

public sealed record CodecMismatchProbeRes(
    bool Rejected,
    string? FailureType,
    string? Value);

public sealed record EvidenceWaitReq(
    string[] ContainsAll,
    int TimeoutMilliseconds = 10000);

[MessagePackObject]
public sealed class PackedEchoReq
{
    [Key(0)] public string Value { get; set; } = string.Empty;
}

[MessagePackObject]
public sealed class PackedEchoMsg
{
    [Key(0)] public string CommandId { get; set; } = string.Empty;

    [Key(1)] public string Value { get; set; } = string.Empty;
}