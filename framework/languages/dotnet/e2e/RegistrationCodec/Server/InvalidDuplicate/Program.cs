using RegistrationCodec.Server.InvalidDuplicate;
using RegistrationCodec.Server.InvalidDuplicate.Handlers;
using RegistrationCodec.Server.InvalidDuplicate.Infrastructure;

var app = RegistrationCodecServerHostFactory.Create(args);
await app.RunAsync();
