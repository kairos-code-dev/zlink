namespace Zlink.Framework.Contracts.Handlers;

public delegate ValueTask<object?> ZLinkHandlerDelegate(CancellationToken cancellationToken);