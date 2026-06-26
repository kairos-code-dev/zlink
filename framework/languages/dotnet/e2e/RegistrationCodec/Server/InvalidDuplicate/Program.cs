using RegistrationCodec.Server;
using RegistrationCodec.Server.InvalidDuplicate;

var serverArgs = args.Concat(["--invalid-mode", "duplicate"]).ToArray();
await RegistrationCodecServerHostFactory.Create(serverArgs).RunAsync();
