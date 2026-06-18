type SupportChatRouteHeader = {
  name: string;
};

type SupportChatSessionContext = {
  actorId?: string | null;
  displayName?: string | null;
  role?: string | null;
};

type SupportChatSessionHandlers = {
  tryHandle(context: SupportChatSessionContext, header: SupportChatRouteHeader, payload: unknown): Promise<boolean>;
  relay(context: SupportChatSessionContext, header: SupportChatRouteHeader, payload: unknown): Promise<unknown>;
};

// The Session server owns authentication and stream session lifecycle but never interprets
// conversation rules. AuthenticateReq is handled as a session lifecycle packet; every other
// packet is relayed to the bound Support actor through the Support channel.
class SupportChatSession {
  constructor(
    readonly context: SupportChatSessionContext,
    private readonly handlers: SupportChatSessionHandlers
  ) {}

  async dispatch(header: SupportChatRouteHeader, payload: unknown): Promise<unknown> {
    if (await this.handlers.tryHandle(this.context, header, payload)) {
      return undefined;
    }
    this.requireBound(`relaying packet '${header.name}'`);
    return await this.handlers.relay(this.context, header, payload);
  }

  onDisconnected(): void {
    this.context.actorId = null;
    this.context.displayName = null;
    this.context.role = null;
  }

  private requireBound(action: string): void {
    if (this.context.actorId === null || this.context.actorId === undefined) {
      throw new Error(`Client must authenticate before ${action}.`);
    }
  }
}

export { SupportChatSession };
