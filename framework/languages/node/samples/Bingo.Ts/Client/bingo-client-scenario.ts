const {
  PacketNames,
  authenticateReq,
  deterministicCard,
  matchBingoReq,
  submitBingoCardReq
} = require('../Shared/Contracts/messages');
import type {
  AuthenticateSessionRes,
  MatchBingoRes,
  NumberDrawnNotify,
  PlayerJoinedNotify,
  StateEnvelope,
  SubmitBingoCardRes
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector } from '../../../packages/stream-connector/dist';

type BingoRoomState = {
  roomId: string;
  status: string;
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

    const client1Auth = await client1.request(authenticateReq('player-1')).submit<AuthenticateSessionRes>(signal);
    const client2Auth = await client2.request(authenticateReq('player-2')).submit<AuthenticateSessionRes>(signal);

    ensure(() => client1Auth.actorId === 'player-1');
    ensure(() => client2Auth.actorId === 'player-2');
    ensure(() => client2Auth.actorId !== client1Auth.actorId);

    const client1PlayerJoinedNotifies: PlayerJoinedNotify[] = [];
    client1.on<PlayerJoinedNotify>(PacketNames.playerJoinedNotify, (message) => {
      client1PlayerJoinedNotifies.push(message.payload);
    });

    // 2. player-1 matches first, gets a waiting room, and receives no self-join notify.
    const client1MatchRes = await client1.request(matchBingoReq()).submit<MatchBingoRes>(signal);

    ensure(() => client1MatchRes.roomId.length > 0);
    ensure(() => stateOf(client1MatchRes).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client1MatchRes).status === 'WaitingForPlayers');
    ensure(() => stateOf(client1MatchRes).hostActorId === client1Auth.actorId);
    await new Promise((resolve) => setImmediate(resolve));
    ensure(() => client1PlayerJoinedNotifies.length === 0);

    const client2PlayerJoinedNotifies: PlayerJoinedNotify[] = [];
    client2.on<PlayerJoinedNotify>(PacketNames.playerJoinedNotify, (message) => {
      client2PlayerJoinedNotifies.push(message.payload);
    });

    // 3-5. player-2 joins the same room; player-1 observes join and both clients observe start.
    const client1SawClient2Join = client1
      .waitFor<PlayerJoinedNotify>(PacketNames.playerJoinedNotify)
      .where((message) => message.payload.actorId === client2Auth.actorId)
      .submit(signal);
    const client1StartedTask = client1.waitFor<StateEnvelope>(PacketNames.gameStartedNotify).submit(signal);
    const client2StartedTask = client2.waitFor<StateEnvelope>(PacketNames.gameStartedNotify).submit(signal);

    const client2MatchRes = await client2.request(matchBingoReq()).submit<MatchBingoRes>(signal);

    ensure(() => client2MatchRes.roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2MatchRes).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2MatchRes).status === 'Running');
    await new Promise((resolve) => setImmediate(resolve));
    ensure(() => client2PlayerJoinedNotifies.length === 0);

    const [client1Joined, client1Started, client2Started] = await Promise.all([
      client1SawClient2Join,
      client1StartedTask,
      client2StartedTask
    ]);
    ensure(() => client1Joined.payload.roomId === client1MatchRes.roomId);
    ensure(() => client1Joined.payload.actorId === client2Auth.actorId);
    ensure(() => stateOf(client1Joined.payload).status === 'Running');
    ensure(() => stateOf(client1Joined.payload).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client1Started.payload).status === 'Running');
    ensure(() => stateOf(client1Started.payload).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2Started.payload).status === 'Running');
    ensure(() => stateOf(client2Started.payload).roomId === client1MatchRes.roomId);

    // 6. Both clients submit deterministic cards and responses show both 3 x 3 cards.
    const client2Card = await client2
      .request(submitBingoCardReq(client2MatchRes.roomId, deterministicCard(client2Auth.actorId)))
      .submit<SubmitBingoCardRes>(signal);

    ensure(() => stateOf(client2Card).status === 'Running');
    ensure(() => stateOf(client2Card).players.find((player) => player.actorId === client2Auth.actorId)?.card.length === 9);

    const drawSeq1ForClient1 = client1
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 1)
      .submit(signal);
    const drawSeq1ForClient2 = client2
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 1)
      .submit(signal);
    const drawSeq2ForClient1 = client1
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 2)
      .submit(signal);
    const drawSeq2ForClient2 = client2
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 2)
      .submit(signal);
    const drawSeq3ForClient1 = client1
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 3)
      .submit(signal);
    const drawSeq3ForClient2 = client2
      .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
      .where((message) => message.payload.drawSeq === 3)
      .submit(signal);
    const client1EndedTask = client1.waitFor<StateEnvelope>(PacketNames.gameEndedNotify).submit(signal);
    const client2EndedTask = client2.waitFor<StateEnvelope>(PacketNames.gameEndedNotify).submit(signal);

    const client1Card = await client1
      .request(submitBingoCardReq(client1MatchRes.roomId, deterministicCard(client1Auth.actorId)))
      .submit<SubmitBingoCardRes>(signal);

    ensure(() => stateOf(client1Card).status === 'Running');
    ensure(() => stateOf(client1Card).players.every((player) => player.card.length === 9));
    ensure(() => stateOf(client1Card).players.length === 2);

    // 7. Number drawing is server-driven; clients only wait for draw notifications.
    const [client1Draw1, client2Draw1] = await Promise.all([drawSeq1ForClient1, drawSeq1ForClient2]);
    requireSameDraw(client1Draw1.payload, client2Draw1.payload, 1);
    const drawnNumbers = [client1Draw1.payload];

    if (stateOf(client1Draw1.payload).status !== 'Finished') {
      const [client1Draw2, client2Draw2] = await Promise.all([drawSeq2ForClient1, drawSeq2ForClient2]);
      requireSameDraw(client1Draw2.payload, client2Draw2.payload, 2);
      drawnNumbers.push(client1Draw2.payload);
    }

    if (stateOf(drawnNumbers[drawnNumbers.length - 1]).status !== 'Finished') {
      const [client1Draw3, client2Draw3] = await Promise.all([drawSeq3ForClient1, drawSeq3ForClient2]);
      requireSameDraw(client1Draw3.payload, client2Draw3.payload, 3);
      drawnNumbers.push(client1Draw3.payload);
    }

    ensure(() => drawnNumbers.length > 0);
    ensure(() => stateOf(drawnNumbers[drawnNumbers.length - 1]).status === 'Finished');

    // 8. Both clients receive the final finished state when the server detects bingo.
    const [client1Ended, client2Ended] = await Promise.all([client1EndedTask, client2EndedTask]);
    ensure(() => stateOf(client1Ended.payload).status === 'Finished');
    ensure(() => stateOf(client2Ended.payload).status === 'Finished');
    ensure(() => stateOf(client2Ended.payload).drawnNumbers.join(',') === stateOf(client1Ended.payload).drawnNumbers.join(','));
    ensure(() => stateOf(client2Ended.payload).winners.join(',') === stateOf(client1Ended.payload).winners.join(','));
    ensure(() =>
      stateOf(client2Ended.payload).players.map((player) => player.actorId).join(',') ===
      stateOf(client1Ended.payload).players.map((player) => player.actorId).join(','));

    const started = stateOf(client1Started.payload);
    const ended = stateOf(client1Ended.payload);
    ensure(() => started.status === 'Running');
    ensure(() => ended.status === 'Finished');
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
