import {
  BingoRewardItems,
  BingoRoomStatus,
  BingoSamplePlayers,
  PacketNames,
  deterministicCard
} from '../Shared/Contracts/messages';
import {
  AuthenticateReq,
  MatchBingoReq,
  ObserveBingoEventsReq,
  StopObservingBingoEventsReq,
  SubmitBingoCardReq
} from '../Shared/Contracts/bingo-messages.generated';
import type {
  AuthenticateRes,
  BingoRewardAnnouncedNotify,
  MatchBingoRes,
  NumberDrawnNotify,
  ObserveBingoEventsRes,
  PlayerJoinedNotify,
  StateEnvelope,
  StopObservingBingoEventsRes,
  SubmitBingoCardRes
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector, ZlinkStreamMessage } from '@zlink-systems/stream-connector';

type BingoRoomState = {
  roomId: string;
  status: BingoRoomStatus;
  hostActorId: string | null;
  drawSeq: number;
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
    observer: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    // 1. Clients connect only to Session streams, authenticate, and verify actor ids.
    await client1.connect(signal);
    await client2.connect(signal);
    await observer.connect(signal);

    const client1Auth = await client1.request(new AuthenticateReq({ accessToken: BingoSamplePlayers.player1 }))
      .packetName(PacketNames.authenticateReq).submit<AuthenticateRes>(signal);
    const client2Auth = await client2.request(new AuthenticateReq({ accessToken: BingoSamplePlayers.player2 }))
      .packetName(PacketNames.authenticateReq).submit<AuthenticateRes>(signal);
    const observerAuth = await observer.request(new AuthenticateReq({ accessToken: BingoSamplePlayers.observer }))
      .packetName(PacketNames.authenticateReq).submit<AuthenticateRes>(signal);

    ensure(() => client1Auth.actorId === BingoSamplePlayers.player1);
    ensure(() => client2Auth.actorId === BingoSamplePlayers.player2);
    ensure(() => observerAuth.actorId === BingoSamplePlayers.observer);
    ensure(() => client1Auth.actorNodeRid.length > 0);
    ensure(() => client2Auth.actorNodeRid.length > 0);
    ensure(() => observerAuth.actorNodeRid.length > 0);
    ensure(() => client2Auth.actorId !== client1Auth.actorId);
    ensure(() => client2Auth.actorNodeRid !== client1Auth.actorNodeRid);

    // 2. player-1 matches first, gets a waiting room, and receives no self-join notify.
    const client1SelfJoinNotify = whileNoMessage(
      client1,
      PacketNames.playerJoinedNotify,
      (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client1Auth.actorId,
      () => client1.request(new MatchBingoReq({ mode: 'two-player' }))
        .packetName(PacketNames.matchBingoReq).submit<MatchBingoRes>(signal),
      signal
    );
    const client1MatchRes = await client1SelfJoinNotify;

    ensure(() => client1MatchRes.roomId.length > 0);
    ensure(() => stateOf(client1MatchRes).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client1MatchRes).status === BingoRoomStatus.WaitingForPlayers);
    ensure(() => stateOf(client1MatchRes).hostActorId === client1Auth.actorId);
    ensure(() => client1MatchRes.roomOwnerNodeRid === client1Auth.actorNodeRid);

    // 3. The third client observes rewards through a local BingoRoom on SessionB's Play node.
    const observed = await observer
      .request(new ObserveBingoEventsReq({ roomId: client1MatchRes.roomId }))
      .packetName(PacketNames.observeBingoEventsReq)
      .submit<ObserveBingoEventsRes>(signal);
    ensure(() => observed.subscribed);
    ensure(() => observed.observerNodeRid === observerAuth.actorNodeRid);
    ensure(() => observed.observerNodeRid !== client1MatchRes.roomOwnerNodeRid);
    const observerRewardTask = observer
      .waitFor<BingoRewardAnnouncedNotify>(PacketNames.rewardAnnouncedNotify)
      .where((message) => message.payload.roomId === client1MatchRes.roomId)
      .submit(signal);

    // 4-6. player-2 joins the same room; player-1 observes join and both clients observe start.
    const client1SawClient2Join = client1
      .waitFor<PlayerJoinedNotify>(PacketNames.playerJoinedNotify)
      .where((message) => message.payload.actorId === client2Auth.actorId)
      .submit(signal);
    const client1StartedTask = client1.waitFor<StateEnvelope>(PacketNames.gameStartedNotify).submit(signal);
    const client2StartedTask = client2.waitFor<StateEnvelope>(PacketNames.gameStartedNotify).submit(signal);
    const client2MatchResTask = whileNoMessage(
      client2,
      PacketNames.playerJoinedNotify,
      (message: ZlinkStreamMessage<PlayerJoinedNotify>) => message.payload.actorId === client2Auth.actorId,
      () => client2.request(new MatchBingoReq({ mode: 'two-player' }))
        .packetName(PacketNames.matchBingoReq).submit<MatchBingoRes>(signal),
      signal
    );
    const client2MatchRes = await client2MatchResTask;

    ensure(() => client2MatchRes.roomId === client1MatchRes.roomId);
    ensure(() => client2MatchRes.roomOwnerNodeRid === client1MatchRes.roomOwnerNodeRid);
    ensure(() => client2Auth.actorNodeRid !== client2MatchRes.roomOwnerNodeRid);
    ensure(() => stateOf(client2MatchRes).roomId === client1MatchRes.roomId);
    ensure(() => stateOf(client2MatchRes).status === BingoRoomStatus.Running);

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

    // 7. Both clients submit deterministic cards and responses show both 3 x 3 cards.
    const client2Card = await client2
      .request(new SubmitBingoCardReq({
        roomId: client2MatchRes.roomId,
        card: deterministicCard(client2Auth.actorId)
      }))
      .packetName(PacketNames.submitBingoCardReq)
      .submit<SubmitBingoCardRes>(signal);

    ensure(() => stateOf(client2Card).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client2Card).players.find((player) => player.actorId === client2Auth.actorId)?.card.length === 9);

    const drawWaitController = new AbortController();
    const abortDrawWaits = () => drawWaitController.abort();
    signal?.addEventListener('abort', abortDrawWaits, { once: true });
    const drawTasks = Array.from({ length: 15 }, (_, index) => {
      const drawSeq = index + 1;
      return {
        drawSeq,
        client1: waitForDraw(client1, drawSeq, drawWaitController.signal),
        client2: waitForDraw(client2, drawSeq, drawWaitController.signal)
      };
    });
    const client1EndedTask = client1.waitFor<StateEnvelope>(PacketNames.gameEndedNotify).submit(signal);
    const client2EndedTask = client2.waitFor<StateEnvelope>(PacketNames.gameEndedNotify).submit(signal);

    const client1Card = await client1
      .request(new SubmitBingoCardReq({
        roomId: client1MatchRes.roomId,
        card: deterministicCard(client1Auth.actorId)
      }))
      .packetName(PacketNames.submitBingoCardReq)
      .submit<SubmitBingoCardRes>(signal);

    ensure(() => stateOf(client1Card).status === BingoRoomStatus.Running);
    ensure(() => stateOf(client1Card).players.every((player) => player.card.length === 9));
    ensure(() => stateOf(client1Card).players.length === 2);

    // 8. Number drawing is server-driven; clients only wait for draw notifications.
    const drawnNumbers: NumberDrawnNotify[] = [];
    try {
      for (const drawTask of drawTasks) {
        const [client1Draw, client2Draw] = await Promise.all([
          drawTask.client1,
          drawTask.client2
        ]);
        requireSameDraw(client1Draw.payload, client2Draw.payload, drawTask.drawSeq);
        drawnNumbers.push(client1Draw.payload);
        if (stateOf(client1Draw.payload).status === BingoRoomStatus.Finished) {
          break;
        }
      }
    } finally {
      drawWaitController.abort();
      signal?.removeEventListener('abort', abortDrawWaits);
      await Promise.allSettled(drawTasks.flatMap((drawTask) => [drawTask.client1, drawTask.client2]));
    }

    ensure(() => drawnNumbers.length > 0);
    ensure(() => stateOf(drawnNumbers[drawnNumbers.length - 1]).status === BingoRoomStatus.Finished);

    // 9. Both clients receive the final finished state when the server detects bingo.
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

    // 10-11. Observer receives the reward through Spot pub/sub and then stops observing.
    const reward = await observerRewardTask;
    ensure(() => reward.payload.roomId === client1MatchRes.roomId);
    ensure(() => reward.payload.actorId === client1Auth.actorId);
    ensure(() => reward.payload.drawSeq === ended.drawSeq);
    ensure(() => reward.payload.itemId === BingoRewardItems.goldenDauberId);
    ensure(() => reward.payload.itemName === BingoRewardItems.goldenDauberName);
    ensure(() => reward.payload.rarity === BingoRewardItems.legendaryRarity);
    ensure(() => reward.payload.receivingSpotNodeRid === observed.observerNodeRid);
    ensure(() => reward.payload.receivingSpotNodeRid !== client1MatchRes.roomOwnerNodeRid);

    const stopped = await observer
      .request(new StopObservingBingoEventsReq({ roomId: client1MatchRes.roomId }))
      .packetName(PacketNames.stopObservingBingoEventsReq)
      .submit<StopObservingBingoEventsRes>(signal);
    ensure(() => stopped.stopped);
    ensure(() => stopped.observerNodeRid === observed.observerNodeRid);
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

function waitForDraw(
  client: ZlinkStreamConnector,
  drawSeq: number,
  signal?: AbortSignal
): Promise<ZlinkStreamMessage<NumberDrawnNotify>> {
  return client
    .waitFor<NumberDrawnNotify>(PacketNames.numberDrawnNotify)
    .where((message) => message.payload.drawSeq === drawSeq)
    .submit(signal);
}

async function whileNoMessage<TPayload, TResult>(
  client: ZlinkStreamConnector,
  packetName: string,
  predicate: (message: ZlinkStreamMessage<TPayload>) => boolean,
  operation: () => Promise<TResult>,
  signal?: AbortSignal
): Promise<TResult> {
  const boundary = new AbortController();
  const abort = () => boundary.abort();
  signal?.addEventListener('abort', abort, { once: true });
  const absence = client
    .waitFor<TPayload>(packetName)
    .where(predicate)
    .submit(boundary.signal)
    .then(() => {
      throw new Error(`Unexpected stream message '${packetName}'.`);
    })
    .catch((error) => {
      if (boundary.signal.aborted) return;
      throw error;
    });
  try {
    const result = await operation();
    boundary.abort();
    await absence;
    return result;
  } catch (operationError) {
    boundary.abort();
    await Promise.allSettled([absence]);
    throw operationError;
  } finally {
    signal?.removeEventListener('abort', abort);
  }
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
