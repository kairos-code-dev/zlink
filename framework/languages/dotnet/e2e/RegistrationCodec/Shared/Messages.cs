using MessagePack;
using Zlink.Framework.Contracts.Handlers;

namespace RegistrationCodec.Shared;

public static class RegistrationCodecNames
{
    public const string Channel = "reg-codec";
}

public sealed record EchoReq(string Value);

public sealed record EchoReply(string Value, string ContentType);

public sealed record EchoCommand(string CommandId, string Value);

[ZLinkPacket("EchoAuto")]
public sealed record EchoAutoReq(string Value);

[ZLinkPacket("EchoAutoCommand")]
public sealed record EchoAutoCommand(string CommandId, string Value);

[ZLinkPacket("EchoManual")]
public sealed record EchoManualReq(string Value);

[ZLinkPacket("EchoManualCommand")]
public sealed record EchoManualCommand(string CommandId, string Value);

public sealed record JsonEchoReq(string Value);

public sealed record JsonEchoCommand(string CommandId, string Value);

[MessagePackObject]
public sealed class PackedEchoReq
{
    [Key(0)]
    public string Value { get; set; } = string.Empty;
}

[MessagePackObject]
public sealed class PackedEchoCommand
{
    [Key(0)]
    public string CommandId { get; set; } = string.Empty;

    [Key(1)]
    public string Value { get; set; } = string.Empty;
}
