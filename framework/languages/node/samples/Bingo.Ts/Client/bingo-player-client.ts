const connector = require('../../../../packages/stream-connector/dist');
const json = require('../../../../packages/stream-connector-json/dist');
const {
  PacketNames,
  actorDisplayName,
  authenticateReq,
  deterministicCard,
  matchBingoReq,
  submitBingoCardReq
} = require('../Shared/Contracts/messages');
const { SampleTimings } = require('../Shared/Configuration/sample-names');
const { BingoNotificationInbox } = require('./bingo-notification-inbox');
import type {
  AuthenticateSessionRes,
  MatchBingoRes,
  SubmitBingoCardRes
} from '../Shared/Contracts/messages';

class BingoPlayerClient {
  [key: string]: any;
  constructor(actorId: string, sessionEndpoint: string) {
    this.actorId = actorId;
    this.displayName = actorDisplayName(actorId);
    this.notifications = new BingoNotificationInbox(actorId);
    this.stream = connector.zlinkStreamConnectorFactory.create({
      endpoint: sessionEndpoint,
      dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
      requestTimeoutMs: SampleTimings.requestTimeout,
      heartbeat: { enabled: false }
    });
    for (const packetName of [
      PacketNames.playerJoinedNotify,
      PacketNames.gameStartedNotify,
      PacketNames.numberDrawnNotify,
      PacketNames.gameEndedNotify
    ]) {
      json.onJson(this.stream, packetName, (message: { payload: unknown }) => {
        this.notifications.apply([{ actorId: this.actorId, packetName, payload: message.payload }]);
      });
    }
  }

  async connect(): Promise<void> {
    await this.stream.connect();
  }

  async close(): Promise<void> {
    await this.stream.close();
  }

  async authenticate(): Promise<AuthenticateSessionRes> {
    const authenticated = await this.request(PacketNames.authenticateReq, authenticateReq(this.actorId));
    this.displayName = authenticated.displayName;
    return authenticated;
  }

  async match(): Promise<MatchBingoRes> {
    return await this.request(PacketNames.matchBingoReq, matchBingoReq());
  }

  async submitCard(roomId: string): Promise<SubmitBingoCardRes> {
    return await this.request(PacketNames.submitBingoCardReq, submitBingoCardReq(roomId, deterministicCard(this.actorId)));
  }

  async request(packetName: string, payload: unknown): Promise<any> {
    try {
      return await json
        .requestJson(this.stream, payload)
        .packetName(packetName)
        .timeout(SampleTimings.requestTimeout)
        .submit();
    } catch (error) {
      throw new Error(`${this.actorId} ${packetName} failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
}

export { BingoPlayerClient };
