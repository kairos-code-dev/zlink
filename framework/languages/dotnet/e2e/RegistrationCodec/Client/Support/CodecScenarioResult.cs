using RegistrationCodec.Shared;

namespace RegistrationCodec.Client.Support;

internal sealed record CodecScenarioResult(EchoReply Json, string ProtobufValue, string MessagePackValue);
