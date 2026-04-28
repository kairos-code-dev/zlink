namespace Zlink.Framework.Handlers;

public delegate ValueTask<object?> ZLinkHandlerDelegate(CancellationToken cancellationToken);
