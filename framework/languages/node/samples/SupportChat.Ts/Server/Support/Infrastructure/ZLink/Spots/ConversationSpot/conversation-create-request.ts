type ConversationCreateRequest = {
  customerActorId: string;
  customerDisplayName: string;
  subject: string;
  createdAtUnixMs: number;
};

function conversationCreateRequest(
  customerActorId: string,
  customerDisplayName: string,
  subject: string,
  createdAtUnixMs: number
): ConversationCreateRequest {
  return { customerActorId, customerDisplayName, subject, createdAtUnixMs };
}

export { conversationCreateRequest };
export type { ConversationCreateRequest };
