using YieldDispatch.Server.Registry;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();
