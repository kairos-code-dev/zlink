using PubSub.Shared;
using Zlink.Framework.Contracts.Channels;

namespace PubSub.Server.Publisher.Endpoints;

internal static class PublisherEndpoints
{
    public static WebApplication MapPublisherEndpoints(this WebApplication app)
    {
        app.MapPost("/publish/event", (
            string topic,
            string runId,
            int sequence,
            string value,
            IZLinkFanoutClient fanout,
            CancellationToken cancellationToken) =>
        {
            fanout.Publish(
                    PubSubNames.Channel,
                    topic,
                    new EventMsg(runId, sequence, value))
                .TrySubmit();
            return Results.Ok(new { status = "published", topic, runId, sequence });
        });
        app.MapPost("/publish/missing", (
            string topic,
            string runId,
            int sequence,
            string value,
            IZLinkFanoutClient fanout,
            CancellationToken cancellationToken) =>
        {
            fanout.Publish(
                    PubSubNames.Channel,
                    topic,
                    new MissingEventMsg(runId, sequence, value))
                .TrySubmit();
            return Results.Ok(new { status = "published", topic, runId, sequence });
        });
        return app;
    }
}
