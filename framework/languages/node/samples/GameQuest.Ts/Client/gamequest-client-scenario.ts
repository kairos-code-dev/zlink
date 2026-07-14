import {
  PacketNames,
  QuestIds,
  QuestStatuses,
  collectItemReq,
  completeMissionReq,
  enterAreaReq,
  getQuestProgressReq,
  joinSessionReq,
  killMonsterReq,
  syncQuestProgressReq,
  unlockFeatureReq
} from '../Shared/Contracts/messages';
import type { BrowserHttpClient } from '../../browser-client-runtime';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type {
  CollectItemRes,
  CompleteMissionRes,
  EnterAreaRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  JoinSessionRes,
  KillMonsterRes,
  QuestCompletedNotify,
  QuestProgress,
  QuestProgressNotify,
  SyncQuestProgressRes,
  UnlockFeatureRes
} from '../Shared/Contracts/messages';

class GameQuestClientScenario {
  async run(
    apiA: BrowserHttpClient,
    apiB: BrowserHttpClient,
    missionA: BrowserHttpClient,
    missionB: BrowserHttpClient,
    apiAStream: ZlinkStreamConnector,
    apiBStream: ZlinkStreamConnector,
    apiBReconnectStream: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    await Promise.all([apiAStream.connect(signal), apiBStream.connect(signal)]);
    const [joined, bobJoined] = await Promise.all([
      apiAStream.request(joinSessionReq('player-alice'), Object)
        .packetName(PacketNames.joinSessionReq)
        .submit<JoinSessionRes>(signal),
      apiBStream.request(joinSessionReq('player-bob'), Object)
        .packetName(PacketNames.joinSessionReq)
        .submit<JoinSessionRes>(signal)
    ]);
    ensure(() => joined.activeQuests.length === 0);
    ensure(() => bobJoined.activeQuests.length === 0);

    const wrongArea = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'desert', 'kill-wrong-area'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => wrongArea.eventId === 'player-alice-kill-wrong-area');
    await expectNoPush(apiAStream, PacketNames.questProgressNotify, signal);
    await expectRequestFailure(
      apiBStream.request(collectItemReq('player-bob', 'healing-herb', -1, 'invalid-negative-count'), Object)
        .packetName(PacketNames.collectItemReq)
        .submit<CollectItemRes>(signal)
    );
    await expectNoPush(apiBStream, PacketNames.questProgressNotify, signal);

    const firstProgress = apiAStream.waitFor<QuestProgressNotify>(PacketNames.questProgressNotify)
      .where((message) => message.payload.playerId === 'player-alice')
      .submit(signal);
    const firstHerbProgress = apiBStream.waitFor<QuestProgressNotify>(PacketNames.questProgressNotify)
      .where((message) => message.payload.playerId === 'player-bob')
      .submit(signal);
    const [firstKill, offlineItem] = await Promise.all([
      apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-1'), Object)
        .packetName(PacketNames.killMonsterReq)
        .submit<KillMonsterRes>(signal),
      apiBStream.request(collectItemReq('player-bob', 'healing-herb', 1, 'herb-1'), Object)
        .packetName(PacketNames.collectItemReq)
        .submit<CollectItemRes>(signal)
    ]);
    ensure(() => firstKill.eventId === 'player-alice-kill-1');
    ensure(() => offlineItem.eventId === 'player-bob-herb-1');
    const firstProgressPush = await firstProgress;
    const firstHerbPush = await firstHerbProgress;
    ensure(() => firstProgressPush.payload.progress.questId === QuestIds.FirstHunt);
    ensure(() => firstProgressPush.payload.progress.currentCount === 1);
    ensure(() => !('targetConnectionId' in firstProgressPush.payload));
    ensure(() => firstHerbPush.payload.progress.questId === QuestIds.HerbGathering);
    ensure(() => firstHerbPush.payload.progress.currentCount === 1);
    const completeFirstHunt = waitForQuestCompletion(apiAStream, QuestIds.FirstHunt, signal);
    await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-2'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    const thirdKill = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => thirdKill.eventId === 'player-alice-kill-3');
    const firstHuntPush = await completeFirstHunt;
    ensure(() => firstHuntPush.payload.rewardGranted);
    ensure(() => !('targetConnectionId' in firstHuntPush.payload));
    ensure(() => firstHuntPush.payload.progress.status === QuestStatuses.RewardGranted);

    const duplicate = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-3'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => duplicate.eventId === thirdKill.eventId);
    const afterCompletion = await apiAStream.request(killMonsterReq('player-alice', 'wolf', 'forest', 'kill-4'), Object)
      .packetName(PacketNames.killMonsterReq)
      .submit<KillMonsterRes>(signal);
    ensure(() => afterCompletion.eventId === 'player-alice-kill-4');
    await expectNoPush(apiAStream, PacketNames.questCompletedNotify, signal);

    const auctionComplete = waitForQuestCompletion(apiAStream, QuestIds.OpenAuction, signal);
    const auction = await apiAStream.request(unlockFeatureReq('player-alice', 'auction', 'unlock-auction'), Object)
      .packetName(PacketNames.unlockFeatureReq)
      .submit<UnlockFeatureRes>(signal);
    ensure(() => auction.eventId === 'player-alice-unlock-auction');
    const auctionCompletePush = await auctionComplete;
    ensure(() => auctionCompletePush.payload.rewardGranted);
    const snapshot = await apiA.post('/internal/snapshot')
      .body({ playerId: 'player-alice' })
      .fetch<GetGameplaySnapshotRes>();
    ensure(() => snapshot.unlockedFeatureIds.includes('auction'));
    ensure(() => snapshot.killCounts.some((entry) => entry.monsterId === 'wolf' && entry.areaId === 'desert' && entry.count === 1));

    await apiAStream.close();
    await apiBReconnectStream.connect(signal);
    const aliceRejoined = await apiBReconnectStream.request(joinSessionReq('player-alice'), Object)
      .packetName(PacketNames.joinSessionReq)
      .submit<JoinSessionRes>(signal);
    ensure(() => aliceRejoined.activeQuests.some((progress) =>
      progress.questId === QuestIds.FirstHunt && progress.status === QuestStatuses.RewardGranted));
    const beforeDeactivate = requireQuest(aliceRejoined.activeQuests, QuestIds.FirstHunt);

    const closeOwnerA = await missionA.post('/self-check/owner/player-alice/close').fetch<{ closed: boolean }>();
    const closeOwnerB = await missionB.post('/self-check/owner/player-alice/close').fetch<{ closed: boolean }>();
    ensure(() => closeOwnerA.closed || closeOwnerB.closed);

    const earlyRuins = await apiBReconnectStream.request(enterAreaReq('player-alice', 'ruins', 'enter-ruins-too-early'), Object)
      .packetName(PacketNames.enterAreaReq)
      .submit<EnterAreaRes>(signal);
    ensure(() => earlyRuins.eventId === 'player-alice-enter-ruins-too-early');
    const beforeTutorial = await getStreamProjection(apiBReconnectStream, 'player-alice', signal);
    ensure(() => beforeTutorial.every((progress) => progress.questId !== QuestIds.RuinsExplorer));

    const tutorialCompleted = waitForQuestCompletion(apiBReconnectStream, QuestIds.TutorialPath, signal);
    const tutorial = await apiBReconnectStream.request(completeMissionReq('player-alice', 'tutorial', 'mission-tutorial'), Object)
      .packetName(PacketNames.completeMissionReq)
      .submit<CompleteMissionRes>(signal);
    ensure(() => tutorial.eventId === 'player-alice-mission-tutorial');
    await tutorialCompleted;
    const afterReactivate = await getStreamProjection(apiBReconnectStream, 'player-alice', signal);
    const restoredFirstHunt = requireQuest(afterReactivate, QuestIds.FirstHunt);
    ensure(() => restoredFirstHunt.currentCount === beforeDeactivate.currentCount);
    ensure(() => restoredFirstHunt.status === beforeDeactivate.status);
    ensure(() => restoredFirstHunt.version === beforeDeactivate.version);
    const tutorialPath = requireQuest(afterReactivate, QuestIds.TutorialPath);
    ensure(() => tutorialPath.currentCount === 2);
    ensure(() => tutorialPath.status === QuestStatuses.RewardGranted);
    const ruinsBeforeFinalStep = requireQuest(afterReactivate, QuestIds.RuinsExplorer);
    ensure(() => ruinsBeforeFinalStep.currentCount === 1);
    ensure(() => ruinsBeforeFinalStep.status === QuestStatuses.Active);
    console.log('gamequest-rehydrate=completed');
    const ruinsCompleted = waitForQuestCompletion(apiBReconnectStream, QuestIds.RuinsExplorer, signal);
    const ruins = await apiBReconnectStream.request(enterAreaReq('player-alice', 'ruins', 'enter-ruins'), Object)
      .packetName(PacketNames.enterAreaReq)
      .submit<EnterAreaRes>(signal);
    ensure(() => ruins.eventId === 'player-alice-enter-ruins');
    const ruinsPush = await ruinsCompleted;
    ensure(() => ruinsPush.payload.progress.currentCount === 2);
    ensure(() => ruinsPush.payload.progress.status === QuestStatuses.RewardGranted);

    const bobProgress = await waitForStreamProjection(
      apiBStream,
      'player-bob',
      (p) => p.questId === QuestIds.HerbGathering && p.currentCount === 1,
      signal
    );
    ensure(() => bobProgress.some((p) => p.questId === QuestIds.HerbGathering && p.currentCount === 1));

    const herbCompleted = waitForQuestCompletion(apiBStream, QuestIds.HerbGathering, signal);
    const onlineItem = await apiBStream.request(collectItemReq('player-bob', 'healing-herb', 4, 'herb-2'), Object)
      .packetName(PacketNames.collectItemReq)
      .submit<CollectItemRes>(signal);
    ensure(() => onlineItem.eventId === 'player-bob-herb-2');
    const herbPush = await herbCompleted;
    ensure(() => herbPush.payload.playerId === 'player-bob');
    ensure(() => herbPush.payload.rewardGranted);
    ensure(() => herbPush.payload.progress.status === QuestStatuses.RewardGranted);

    const bobAuctionCompleted = waitForQuestCompletion(apiBStream, QuestIds.OpenAuction, signal);
    await apiBStream.request(unlockFeatureReq('player-bob', 'auction', 'bob-unlock-auction'), Object)
      .packetName(PacketNames.unlockFeatureReq)
      .submit<UnlockFeatureRes>(signal);
    await bobAuctionCompleted;
    const beforeRebuild = await getStreamProjection(apiBStream, 'player-bob', signal);
    const auctionBeforeRebuild = requireQuest(beforeRebuild, QuestIds.OpenAuction);
    const herbBeforeRebuild = requireQuest(beforeRebuild, QuestIds.HerbGathering);

    const deleted = await apiA.post(`/self-check/projection/player-bob/${QuestIds.HerbGathering}/delete`).submitRaw();
    ensure(() => deleted.status >= 200 && deleted.status < 300);
    const missingProjection = await getStreamProjection(apiBStream, 'player-bob', signal);
    ensure(() => missingProjection.every((progress) => progress.questId !== QuestIds.HerbGathering));
    ensure(() => requireQuest(missingProjection, QuestIds.OpenAuction).version === auctionBeforeRebuild.version);
    const rebuilt = await apiA.post(`/self-check/projection/player-bob/${QuestIds.HerbGathering}/rebuild`).fetch<QuestProgress>();
    ensure(() => rebuilt.questId === QuestIds.HerbGathering);
    ensure(() => rebuilt.status === QuestStatuses.RewardGranted);
    ensure(() => rebuilt.version === herbBeforeRebuild.version);
    const rebuiltProjection = await getStreamProjection(apiBStream, 'player-bob', signal);
    ensure(() => rebuiltProjection.some((progress) =>
      progress.questId === QuestIds.HerbGathering && progress.status === QuestStatuses.RewardGranted));
    ensure(() => requireQuest(rebuiltProjection, QuestIds.OpenAuction).version === auctionBeforeRebuild.version);

    for (const [key, expected] of [['bob-kill-1', 1], ['bob-kill-2', 2]] as const) {
      await apiBStream.request(killMonsterReq('player-bob', 'wolf', 'forest', key), Object)
        .packetName(PacketNames.killMonsterReq)
        .submit<KillMonsterRes>(signal);
      const progress = await waitForStreamProjection(apiBStream, 'player-bob', (candidate) =>
        candidate.questId === QuestIds.FirstHunt && candidate.currentCount === expected, signal);
      ensure(() => requireQuest(progress, QuestIds.FirstHunt).currentCount === expected);
    }
    const bobMissed = await apiB.post('/self-check/gameplay/kill-without-publish/player-bob').submitRaw();
    ensure(() => bobMissed.status >= 200 && bobMissed.status < 300);
    const bobReconcileCompleted = waitForQuestCompletion(apiBStream, QuestIds.FirstHunt, signal);
    const bobSync = await apiBStream.request(syncQuestProgressReq('player-bob'), Object)
      .packetName(PacketNames.syncQuestProgressReq)
      .submit<SyncQuestProgressRes>(signal);
    const bobReconciled = requireQuest(bobSync.updatedQuests, QuestIds.FirstHunt);
    ensure(() => bobReconciled.currentCount === 3);
    ensure(() => bobReconciled.status === QuestStatuses.RewardGranted);
    const bobReconcilePush = await bobReconcileCompleted;
    ensure(() => bobReconcilePush.payload.progress.status === QuestStatuses.RewardGranted);

    const missed = await apiB.post('/self-check/gameplay/kill-without-publish/player-alice').submitRaw();
    ensure(() => missed.status >= 200 && missed.status < 300);
    const sync = await apiBReconnectStream.request(syncQuestProgressReq('player-alice'), Object)
      .packetName(PacketNames.syncQuestProgressReq)
      .submit<SyncQuestProgressRes>(signal);
    ensure(() => sync.updatedQuests.some((progress) =>
      progress.questId === QuestIds.FirstHunt && progress.currentCount >= 4));
    const reconciled = await waitForStreamProjection(apiBReconnectStream, 'player-alice', (progress) =>
      progress.questId === QuestIds.FirstHunt && progress.currentCount >= 4, signal);
    ensure(() => reconciled.some((progress) => progress.questId === QuestIds.FirstHunt && progress.currentCount >= 4));

    await apiBReconnectStream.close();
    const assertion = await waitForServerAssertion(apiA, signal);
    ensure(() => assertion.passed);
    ensure(() => assertion.evidence.includes('owner:mission-a:player-alice'));
    ensure(() => assertion.evidence.includes('owner:mission-b:player-bob'));
    console.log('gamequest-concurrent-owners=completed');
    console.log('gamequest-server-evidence=completed');
  }
}

async function waitForQuestCompletion(
  connector: ZlinkStreamConnector,
  questId: string,
  signal?: AbortSignal
): Promise<{ payload: QuestCompletedNotify }> {
  try {
    return await connector.waitFor<QuestCompletedNotify>(PacketNames.questCompletedNotify)
      .where((message) => message.payload.progress.questId === questId)
      .submit(signal);
  } catch (cause) {
    throw new Error(`Quest completion notification was not received for '${questId}'.`, { cause });
  }
}

async function waitForServerAssertion(api: BrowserHttpClient, signal?: AbortSignal): Promise<GameQuestServerAssertRes> {
  let last: GameQuestServerAssertRes | undefined;
  for (let i = 0; i < 80; i++) {
    last = await api.post('/self-check/assert').fetch<GameQuestServerAssertRes>();
    if (last.passed) {
      return last;
    }
    await delay(50, signal);
  }
  return last ?? { passed: false, evidence: [] };
}

async function waitForStreamProjection(
  connector: ZlinkStreamConnector,
  playerId: string,
  predicate: (progress: QuestProgress) => boolean,
  signal?: AbortSignal
): Promise<QuestProgress[]> {
  for (let i = 0; i < 80; i++) {
    const projection = await getStreamProjection(connector, playerId, signal);
    if (projection.some(predicate)) {
      return projection;
    }
    await delay(50, signal);
  }
  return await getStreamProjection(connector, playerId, signal);
}

async function getStreamProjection(
  connector: ZlinkStreamConnector,
  playerId: string,
  signal?: AbortSignal
): Promise<QuestProgress[]> {
  const response = await connector.request(getQuestProgressReq(playerId), Object)
    .packetName(PacketNames.getQuestProgressReq)
    .submit<GetQuestProgressRes>(signal);
  return response.activeQuests;
}

function requireQuest(projection: QuestProgress[], questId: string): QuestProgress {
  const progress = projection.find((candidate) => candidate.questId === questId);
  if (progress === undefined) throw new Error(`Quest '${questId}' was not found in the projection.`);
  return progress;
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${condition.toString()}`);
  }
}

async function expectRequestFailure(request: Promise<unknown>): Promise<void> {
  try {
    await request;
  } catch {
    return;
  }
  throw new Error('Expected the server-authoritative gameplay validation to reject the request.');
}

async function expectNoPush(
  connector: ZlinkStreamConnector,
  packetName: string,
  signal?: AbortSignal
): Promise<void> {
  try {
    await connector.waitFor(packetName).timeout(250).submit(signal);
  } catch {
    return;
  }
  throw new Error(`Unexpected '${packetName}' push.`);
}

function delay(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(resolve, ms);
    signal?.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('Operation aborted.', 'AbortError'));
    }, { once: true });
  });
}

export { GameQuestClientScenario };
