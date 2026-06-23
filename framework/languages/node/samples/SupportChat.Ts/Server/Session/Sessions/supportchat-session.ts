import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Configuration/sample-names';
import { retry } from '../../runtime-support';
import { SupportChatSessionAuthenticator } from './Handlers/authenticate-session-handler';
import { PacketNames, supportNotificationsReq, withUserIdentity } from '../../../Shared/Contracts/messages';
import type {
  ZLinkChannelClient,
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionDispatchContext,
  ZLinkSessionContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type {
  AuthenticateReq,
  SupportNotificationBatch
} from '../../../Shared/Contracts/messages';

class SupportChatSession implements ZLinkSession {
  private actorId: string | null = null;
  private displayName: string | null = null;
  private role: string | null = null;
  private notificationCursor = 0;
  private closed = false;
  private notificationPumpStarted = false;

  constructor(
    private readonly authenticator: SupportChatSessionAuthenticator,
    private readonly channelClient: ZLinkChannelClient,
    readonly context: ZLinkSessionContext
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (dispatch.packetName === PacketNames.authenticateReq) {
      const response = await this.authenticator.handle(
        payload.decode<AuthenticateReq>(Object as never),
        this
      );
      await this.context.client.reply(response).submit(signal);
      this.startNotificationPump();
      return;
    }

    this.requireBound(`relaying packet '${dispatch.packetName}'`);
    const response = await this.relayToSupport(dispatch.packetName, payload.decode<object>(Object as never), signal);
    await this.context.client.reply(response).submit(signal);
  }

  async onDisconnected(): Promise<void> {
    this.closed = true;
    this.actorId = null;
    this.displayName = null;
    this.role = null;
  }

  setAuthenticated(actorId: string, displayName: string, role: string): void {
    this.actorId = actorId;
    this.displayName = displayName;
    this.role = role;
  }

  private startNotificationPump(): void {
    if (this.notificationPumpStarted) {
      return;
    }
    this.notificationPumpStarted = true;
    void this.pumpNotifications();
  }

  private async relayToSupport(packetName: string, request: object, signal?: AbortSignal): Promise<unknown> {
    return await this.relayToChannel(SampleNames.supportChannel, packetName, request, signal);
  }

  private async relayToChannel(
    channelName: string,
    packetName: string,
    request: object,
    signal?: AbortSignal
  ): Promise<unknown> {
    if (this.actorId === null || this.displayName === null) {
      throw new Error(`Client must authenticate before relaying packet '${packetName}'.`);
    }
    const payload = withUserIdentity(request, this.actorId, this.displayName);
    return await retry(() => this.channelClient
        .requestToChannel(channelName, payload)
        .packetName(packetName)
        .submit<unknown>(signal), { delayMs: 25, maxAttempts: 200 });
  }

  private async pumpNotifications(): Promise<void> {
    while (!this.closed) {
      if (this.actorId !== null && this.displayName !== null) {
        try {
          const response = await this.relayToChannel(
            SampleNames.notificationChannel,
            PacketNames.supportNotificationsReq,
            supportNotificationsReq(this.notificationCursor)
          ) as SupportNotificationBatch;
          this.notificationCursor = response.nextSeq;
          for (const delivered of response.delivered) {
            await this.context.client
              .send(delivered.payload)
              .packetName(delivered.packetName)
              .metadata('seq', String(delivered.seq))
              .submit();
          }
        } catch {
        }
      }
      await new Promise((resolve) => setImmediate(resolve));
    }
  }

  private requireBound(action: string): void {
    if (this.actorId === null) {
      throw new Error(`Client must authenticate before ${action}.`);
    }
  }
}

class SupportChatSessionFactory implements ZLinkSessionFactory<SupportChatSession> {
  constructor(
    @Inject(SupportChatSessionAuthenticator) private readonly authenticator: SupportChatSessionAuthenticator,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channelClient: ZLinkChannelClient
  ) {}

  create(context: ZLinkSessionContext): SupportChatSession {
    return new SupportChatSession(this.authenticator, this.channelClient, context);
  }
}

export { SupportChatSession, SupportChatSessionFactory };
