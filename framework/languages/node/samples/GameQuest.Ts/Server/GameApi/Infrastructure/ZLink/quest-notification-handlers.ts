import { zlinkEntrySpotActorSendHandler } from '@zlink-systems/nestjs';
import {
  PacketNames,
  QuestCompletedNotify,
  QuestProgressNotify
} from '../../../../Shared/Contracts/messages';
import { GameQuestEntrySpot } from './gamequest-entry-spot';
import { GameQuestPlayerActor } from './gamequest-player-actor';
import type {
  ZLinkEntrySpotActorSendHandler,
  ZLinkSpotActorSendContext
} from '@zlink-systems/framework';

@zlinkEntrySpotActorSendHandler({
  actor: () => GameQuestPlayerActor,
  entrySpot: () => GameQuestEntrySpot,
  packetName: PacketNames.questProgressNotify
})
class QuestProgressNotificationHandler
  implements ZLinkEntrySpotActorSendHandler<GameQuestPlayerActor, QuestProgressNotify> {
  async handle(
    actor: GameQuestPlayerActor,
    _context: ZLinkSpotActorSendContext,
    message: QuestProgressNotify
  ): Promise<void> {
    await actor.push(new QuestProgressNotify(message.playerId, message.progress));
  }
}

@zlinkEntrySpotActorSendHandler({
  actor: () => GameQuestPlayerActor,
  entrySpot: () => GameQuestEntrySpot,
  packetName: PacketNames.questCompletedNotify
})
class QuestCompletedNotificationHandler
  implements ZLinkEntrySpotActorSendHandler<GameQuestPlayerActor, QuestCompletedNotify> {
  async handle(
    actor: GameQuestPlayerActor,
    _context: ZLinkSpotActorSendContext,
    message: QuestCompletedNotify
  ): Promise<void> {
    await actor.push(new QuestCompletedNotify(message.playerId, message.progress, message.rewardGranted));
  }
}

export { QuestCompletedNotificationHandler, QuestProgressNotificationHandler };
