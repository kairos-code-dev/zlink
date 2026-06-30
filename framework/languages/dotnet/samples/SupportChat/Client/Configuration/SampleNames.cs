namespace SupportChat.Client.Configuration;

public static class SampleNames
{
    public static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan IdleTimeout = TimeSpan.FromSeconds(3);
    public static readonly TimeSpan CloseGraceTimeout = TimeSpan.FromSeconds(2);
}

public static class SupportChatRoles
{
    public const string Customer = "Customer";
    public const string Agent = "Agent";
}

public static class ConversationStatuses
{
    public const string WaitingForAgent = "WaitingForAgent";
    public const string Active = "Active";
    public const string WaitingForClose = "WaitingForClose";
    public const string Closed = "Closed";
}