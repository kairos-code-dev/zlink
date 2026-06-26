using RegistrationCodec.Server;

var serverArgs = args.Concat(["--codec-mode", "json-only"]).ToArray();
await RegistrationCodecServerHostFactory.Create(serverArgs).RunAsync();
