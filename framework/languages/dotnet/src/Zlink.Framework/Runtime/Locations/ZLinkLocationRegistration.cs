namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Raw location store registration collected by the options builder. The
/// five store roles must share one physical store. Registration is either
/// one store instance or the in-memory store.
/// </summary>
internal sealed class ZLinkLocationRegistration
{
    private ZLinkInMemoryLocationStore? _inMemoryStore;

    public bool UseInMemoryStores { get; set; }

    /// <summary>One physical store instance providing every store role
    /// (draft 20.2). Optional contracts on the same instance (change stamp,
    /// watch) are registered automatically.</summary>
    public IZLinkLocationStore? StoreInstance { get; set; }

    public IZLinkRelocationStore? RelocationStoreInstance { get; set; }

    public ZLinkLocationOptions Options { get; } = new();

    public bool Enabled => UseInMemoryStores || StoreInstance is not null;

    internal IZLinkLocationStore? ResolveStore()
    {
        if (StoreInstance is not null) return StoreInstance;
        if (!UseInMemoryStores) return null;
        return _inMemoryStore ??= new ZLinkInMemoryLocationStore();
    }
}
