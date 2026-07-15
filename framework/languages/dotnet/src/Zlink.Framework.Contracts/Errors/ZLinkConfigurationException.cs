namespace Zlink.Framework.Contracts.Errors;

public sealed class ZLinkConfigurationException(string message) : InvalidOperationException(message);
