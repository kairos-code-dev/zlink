using RegistryMessaging.Server.Consumer.Configuration;
using RegistryMessaging.Server.Consumer.Endpoints;
using RegistryMessaging.Server.Consumer;

var app = ConsumerHostFactory.Create(args);
await app.RunAsync();
