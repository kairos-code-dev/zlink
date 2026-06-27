using PubSub.Server.Publisher.Configuration;
using PubSub.Server.Publisher.Endpoints;
using PubSub.Server.Publisher;

await PublisherHostFactory.Create(args).RunAsync();
