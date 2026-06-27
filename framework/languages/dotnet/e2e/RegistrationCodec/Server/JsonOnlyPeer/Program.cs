using RegistrationCodec.Server.JsonOnlyPeer.Handlers;
using RegistrationCodec.Server.JsonOnlyPeer.Infrastructure;
using RegistrationCodec.Server.JsonOnlyPeer;

var app = RegistrationCodecServerHostFactory.Create(args);
await app.RunAsync();
