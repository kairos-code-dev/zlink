import { BingoRoomStatus, BingoSamplePlayers, PacketNames, authenticateReq, deterministicCard, matchBingoReq, submitBingoCardReq } from '../Shared/Contracts/messages';
import type {
  AuthenticateSessionRes,
  MatchBingoRes,
  NumberDrawnNotify,
  PlayerJoinedNotify,
  StateEnvelope,
  SubmitBingoCardRes
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector, ZlinkStreamMessage } from '@zlink-systems/stream-connector';

type BingoRoomState = {
  roomId: string;
  status: BingoRoomStatus;
  hostActorId: string | null;
  winners: string[];
  drawnNumbers: number[];
  players: Array<{
    actorId: string;
    card: number[];
    marks: boolean[];
  }>;
};

class BingoClientScenario {
  async run(
    client1: ZlinkStreamConnector,
    client2: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    // 1. Both clients connect to Session, authenticate, and verify their actor ids.
    await client1.connect(signal);
    await client2.connect(signal);

    const client1Auth = await client1.request(authenticateReq(BingoSamplePlayers.player1)).submit<AuthenticateSessionRes>(signal);
    const client2Auth = await client2.request(authenticateReq(BingoSamplePlayers.player2)).submit<AuthenticateSessionRes>(signal);

    ensure(() => client1Auth.actorId === BingoSamplePlayers.player1);
    ensure(() => client2Auth.actorId === BingoSamplePlayers.player2);
    ensure(() => client2Auth.actorId !== client1Auth.actorId);

    // 2. player-1 matches first, gets a waiting room, and receives no self-join notify.
    const client1SelfJoinNotify = expectNoMessage(
      client1,
      PacketNames.playerJoinedNotify,
      (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client1Auth.actorId,
      signal
    );
    const client1MatchRes = await client1.request(matchBingoReq()).submit<MatchBingoRes>(signal);

    ensure(() => client1MatchRes.roomId.length > 0);
    ensure(() => stateOf(client1MatchRes).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client1MatchRes).status === BingoRoomStatus.WaitingForPlayers);
    ensure(() => stateOf(client1MatchRes).hostActorId === client1Auth.actorId);
    await client1SelfJoinNotify;

    // 3-5. player-2 joins the same room; player-1 observes join and both clients observe start.
    const client1SawClient2Join = client1
      .waitFor<PlayerJoinedNotify>(PacketNames.playerJoinedNotify)
      .where((message) => message.payload.actorId === client2Auth.actorId)
      .submit(signal);
    const client2SelfJoinNotify = expectNoMessage(
      client2,
      PacketNames.playerJoinedNotify,
      (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client2Auth.actorId,
      signal
    );
    const client1StartedTask = client1.waitFor<StateEnvelope>(PacketNames.gameStartedNotify).submit(signal);
    const client2StartedTask = client2.waitFor<StateEnvelope>(PacketNames.gameStartedNotify).submit(signal);

    const client2MatchRes = await client2.request(matchBingoReq()).submit<MatchBingoRes>(signal);

    ensure(() => client2MatchRes.roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2MatchRes).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2MatchRes).status === BingoRoomStatus.Running);
    await client2SelfJoinNotify;

    const [client1Joined, client1Started, client2Started] = await Promise.all([
      client1SawClient2Join,
      client1StartedTask,
      client2StartedTask
    ]);
    ensure(() => client1Joined.payload.roomId === client1MatchRes.roomId);
    ensure(() => client1Joined.payload.actorId === client2Auth.actorId);
    ensure(() => stateOf(client1Joined.payload).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client1Joined.payload).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client1Started.payload).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client1Started.payload).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2Started.payload).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client2Started.payload).roomId === client1MatchRes.roomId);

    // 6. Both clients submit deterministic cards and responses show both 3 x 3 cards.
    const client2Card = await client2
      .request(submitBingoCardReq(client2MatchRes.roomId, deterministicCard(client2Auth.actorId)))
      .submit<SubmitBingoCardRes>(signal);

    ensure(() => stateOf(client2Card).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client2Card).players.find((player) => player.actorId === client2Auth.actorId)?.card.length === 9);

    const drawSeq1ForClient1 = client1
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 1)
      .submit(signal);
    const drawSeq1ForClient2 = client2
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 1)
      .submit(signal);
    const drawSeq2ForClient1 = waitForDraw(client1, 2);
    const drawSeq2ForClient2 = waitForDraw(client2, 2);
    const drawSeq3ForClient1 = waitForDraw(client1, 3);
    const drawSeq3ForClient2 = waitForDraw(client2, 3);
    const client1EndedTask = client1.waitFor<StateEnvelope>(PacketNames.gameEndedNotify).submit(signal);
    const client2EndedTask = client2.waitFor<StateEnvelope>(PacketNames.gameEndedNotify).submit(signal);

    const client1Card = await client1
      .request(submitBingoCardReq(client1MatchRes.roomId, deterministicCard(client1Auth.actorId)))
      .submit<SubmitBingoCardRes>(signal);

    ensure(() => stateOf(client1Card).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client1Card).players.every((player) => player.card.length === 9));
    ensure(() => stateOf(client1Card).players.length === 2);

    // 7. Number drawing is server-driven; clients only wait for draw notifications.
    const [client1Draw1, client2Draw1] = await Promise.all([drawSeq1ForClient1, drawSeq1ForClient2]);
    requireSameDraw(client1Draw1.payload, client2Draw1.payload, 1);
    const drawnNumbers = [client1Draw1.payload];

    if (stateOf(client1Draw1.payload).status !== BingoRoomStatus.Finished) {
      const [client1Draw2, client2Draw2] = await Promise.all([drawSeq2ForClient1.promise, drawSeq2ForClient2.promise]);
      requireDraw(client1Draw2, 2);
      requireDraw(client2Draw2, 2);
      requireSameDraw(client1Draw2.payload, client2Draw2.payload, 2);
      drawnNumbers.push(client1Draw2.payload);
    } else {
      drawSeq2ForClient1.cancel();
      drawSeq2ForClient2.cancel();
    }

    if (stateOf(drawnNumbers[drawnNumbers.length - 1]).status !== BingoRoomStatus.Finished) {
      const [client1Draw3, client2Draw3] = await Promise.all([drawSeq3ForClient1.promise, drawSeq3ForClient2.promise]);
      requireDraw(client1Draw3, 3);
      requireDraw(client2Draw3, 3);
      requireSameDraw(client1Draw3.payload, client2Draw3.payload, 3);
      drawnNumbers.push(client1Draw3.payload);
    } else {
      drawSeq3ForClient1.cancel();
      drawSeq3ForClient2.cancel();
    }

    ensure(() => drawnNumbers.length > 0);
    ensure(() => stateOf(drawnNumbers[drawnNumbers.length - 1]).status === BingoRoomStatus.Finished);

    // 8. Both clients receive the final finished state when the server detects bingo.
    const [client1Ended, client2Ended] = await Promise.all([client1EndedTask, client2EndedTask]);
    ensure(() => stateOf(client1Ended.payload).status === BingoRoomStatus.Finished);
    ensure(() => stateOf(client2Ended.payload).status === BingoRoomStatus.Finished);
    ensure(() => stateOf(client2Ended.payload).drawnNumbers.join(',') === stateOf(client1Ended.payload).drawnNumbers.join(','));
    ensure(() => stateOf(client2Ended.payload).winners.join(',') === stateOf(client1Ended.payload).winners.join(','));
    ensure(() =>
      stateOf(client2Ended.payload).players.map((player) => player.actorId).join(',') ===
      stateOf(client1Ended.payload).players.map((player) => player.actorId).join(','));

    const started = stateOf(client1Started.payload);
    const ended = stateOf(client1Ended.payload);
    ensure(() => started.status === BingoRoomStatus.Running);
    ensure(() => ended.status === BingoRoomStatus.Finished);
    ensure(() => drawnNumbers.map((draw) => draw.number).join(',') === ended.drawnNumbers.join(','));
    ensure(() => ended.winners.join(',') === client1Auth.actorId);
    ensure(() => ended.players.every((player) => player.card.length === 9));
    ensure(() => ended.players.every((player) => player.marks[4]));
  }
}

function stateOf(message: { state: unknown } | StateEnvelope | NumberDrawnNotify | PlayerJoinedNotify): BingoRoomState {
  return message.state as BingoRoomState;
}

function requireSameDraw(client1Draw: NumberDrawnNotify, client2Draw: NumberDrawnNotify, expectedSeq: number): void {
  ensure(() => client1Draw.drawSeq === expectedSeq);
  ensure(() => client2Draw.drawSeq === expectedSeq);
  ensure(() => client2Draw.drawSeq === client1Draw.drawSeq);
  ensure(() => client2Draw.number === client1Draw.number);
  ensure(() => stateOf(client2Draw).drawnNumbers.join(',') === stateOf(client1Draw).drawnNumbers.join(','));
}

function waitForDraw(client: ZlinkStreamConnector, drawSeq: number): {
  promise: Promise<ZlinkStreamMessage<NumberDrawnNotify> | null>;
  cancel(): void;
} {
  const controller = new AbortController();
  return {
    promise: client
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === drawSeq)
      .submit(controller.signal)
      .catch((error) => {
        if (controller.signal.aborted) {
          return null;
        }
        throw error;
      }),
    cancel: () => controller.abort()
  };
}

function requireDraw(
  draw: ZlinkStreamMessage<NumberDrawnNotify> | null,
  expectedSeq: number
): asserts draw is ZlinkStreamMessage<NumberDrawnNotify> {
  ensure(() => draw !== null);
  ensure(() => draw.payload.drawSeq === expectedSeq);
}

async function expectNoMessage<TPayload>(
  client: ZlinkStreamConnector,
  packetName: string,
  predicate: (message: ZlinkStreamMessage<TPayload>) => boolean,
  signal?: AbortSignal
): Promise<void> {
  try {
    await client
      .waitFor<TPayload>(packetName)
      .where(predicate)
      .timeout(25)
      .submit(signal);
  } catch (error) {
    if (isRequestTimeout(error)) {
      return;
    }
    throw error;
  }
  throw new Error(`Unexpected stream message '${packetName}'.`);
}

function isRequestTimeout(error: unknown): boolean {
  return typeof error === 'object'
    && error !== null
    && 'error' in error
    && (error as { error?: { code?: unknown } }).error?.code === 'requestTimeout';
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${conditionExpression(condition)}`);
  }
}

function conditionExpression(condition: () => boolean): string {
  return condition
    .toString()
    .replace(/^\s*\(\)\s*=>\s*/, '')
    .replace(/^\s*function\s*\(\)\s*\{\s*return\s*/, '')
    .replace(/;?\s*\}\s*$/, '')
    .trim();
}

export { BingoClientScenario };
