namespace SupportChat.Server.Support.Application.ConversationAssignment;

internal sealed class AgentAssignmentService(AgentAvailabilityDirectory availability)
{
    public AvailableAgent? AssignNextAgent()
    {
        return availability.TakeNext();
    }
}