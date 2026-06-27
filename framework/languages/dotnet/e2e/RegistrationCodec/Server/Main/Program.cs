using RegistrationCodec.Server.Main.Endpoints;
using RegistrationCodec.Server.Main.Handlers;
using RegistrationCodec.Server.Main.Infrastructure;
using RegistrationCodec.Server.Main;

var app = RegistrationCodecServerHostFactory.Create(args);
await app.RunAsync();
