namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal interface IRegistryDiscoveryMetadata
{
    ValueTask PutAsync(
        string key,
        IReadOnlyDictionary<string, string> metadata,
        CancellationToken cancellationToken);

    ValueTask<IRegistryMetadataEntry> GetRequiredAsync(
        string key,
        CancellationToken cancellationToken);

    ValueTask DeleteIfAsync(
        string key,
        IReadOnlyDictionary<string, string> expected,
        CancellationToken cancellationToken);
}

internal interface IRegistryMetadataEntry
{
    string Require(string name);
}
