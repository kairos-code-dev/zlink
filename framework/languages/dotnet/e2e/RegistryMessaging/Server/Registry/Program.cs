using RegistryMessaging.Server.Registry;
using RegistryMessaging.Server.Registry.Configuration;
using RegistryMessaging.Server.Registry.Endpoints;
using RegistryMessaging.Server.Registry.Handlers;
using RegistryMessaging.Server.Registry.Infrastructure;

var app = RegistryHostFactory.Create(args);
await app.RunAsync();
