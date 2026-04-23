namespace Zlink.Framework;

public delegate ValueTask<object?> ZLinkHandlerDelegate(CancellationToken cancellationToken);
