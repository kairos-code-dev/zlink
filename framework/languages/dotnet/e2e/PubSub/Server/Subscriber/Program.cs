using PubSub.Server.Subscriber.Configuration;
using PubSub.Server.Subscriber.Handlers;
using PubSub.Server.Subscriber;

await SubscriberHostFactory.Create(args).RunAsync();
