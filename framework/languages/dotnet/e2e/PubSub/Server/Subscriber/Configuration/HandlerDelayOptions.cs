using PubSub.Server.Subscriber.Handlers;
using PubSub.Server.Subscriber;
namespace PubSub.Server.Subscriber.Configuration;

internal sealed record HandlerDelayOptions(int DelayMs);
