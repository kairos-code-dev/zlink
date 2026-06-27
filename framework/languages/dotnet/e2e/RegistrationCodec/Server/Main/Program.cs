using RegistrationCodec.Server;

var app = RegistrationCodecServerHostFactory.Create(args);
await app.RunAsync();
