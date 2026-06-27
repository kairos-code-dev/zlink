using RegistrationCodec.Shared;

namespace RegistrationCodec.Client;

internal sealed record CodecScenarioResult(EchoReply Json, string ProtobufValue, string MessagePackValue);
