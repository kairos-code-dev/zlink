using RegistrationCodec.CodecRequester;

var app = CodecRequesterHostFactory.Create(args);
await app.RunAsync();
