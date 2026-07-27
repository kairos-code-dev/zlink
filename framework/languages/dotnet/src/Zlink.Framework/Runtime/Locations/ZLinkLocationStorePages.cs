namespace Zlink.Framework.Runtime.Locations;

internal static class ZLinkLocationStorePages
{
    internal static async ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>>
        ListAllMeshNodesAsync(
            this IZLinkLocationStore store,
            string meshName,
            CancellationToken cancellationToken = default)
    {
        var rows = new List<ZLinkMeshNodeDescriptor>();
        string? continuationToken = null;
        do
        {
            var page = await store.ListMeshNodesAsync(
                    meshName,
                    new ZLinkPageRequest(1000, continuationToken),
                    cancellationToken)
                .ConfigureAwait(false);
            rows.AddRange(page.Items);
            continuationToken = page.ContinuationToken;
        } while (continuationToken is not null);

        return rows;
    }
}
