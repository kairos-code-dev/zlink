using PubSub.Shared;
using Zlink.Framework.Contracts.Channels;
using PubSub.Server.Publisher.Configuration;
using PubSub.Server.Publisher;

namespace PubSub.Server.Publisher.Endpoints;

internal static class PublisherEndpoints
{
    public static WebApplication MapPublisherEndpoints(this WebApplication app)
    {
        app.MapPost("/publish/event", async (
            string topic,
            string runId,
            int sequence,
            string value,
            IZLinkFanoutClient fanout,
            CancellationToken cancellationToken) =>
        {
            await fanout.Publish(
                    PubSubNames.Channel,
                    topic,
                    new EventNotify(runId, sequence, value))
                .PacketName("EventNotify")
                .Async(cancellationToken);
            return Results.Ok(new { status = "published", topic, runId, sequence });
        });
        app.MapPost("/publish/missing", async (
            string topic,
            string runId,
            int sequence,
            string value,
            IZLinkFanoutClient fanout,
            CancellationToken cancellationToken) =>
        {
            await fanout.Publish(
                    PubSubNames.Channel,
                    topic,
                    new MissingEventNotify(runId, sequence, value))
                .PacketName("MissingEventNotify")
                .Async(cancellationToken);
            return Results.Ok(new { status = "published", topic, runId, sequence });
        });
        return app;
    }
}
