using RegistryMessaging.Server.Provider;
using RegistryMessaging.Server.Provider.Configuration;
using RegistryMessaging.Server.Provider.Endpoints;
using RegistryMessaging.Server.Provider.Handlers;
using RegistryMessaging.Server.Provider.Infrastructure;

var app = ProviderHostFactory.Create(args);
await app.RunAsync();
