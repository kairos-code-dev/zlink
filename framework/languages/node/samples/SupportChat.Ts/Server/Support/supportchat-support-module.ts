import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../Configuration/sample-names';
import { createSupportChatLocationStore, supportChatLocationOptions } from '../Configuration/location-store';
import { AgentAssignmentService } from './Application/ConversationAssignment/agent-assignment-service';
import { AgentAvailabilityDirectory } from './Application/ConversationAssignment/agent-availability-directory';
import { SupportConversationAllocator } from './Application/ConversationAssignment/support-conversation-allocator';
import { SupportActorDirectory } from './Infrastructure/ZLink/Actors/support-actor-directory';
import { SupportUserActorFactory } from './Infrastructure/ZLink/Actors/support-user-actor-factory';
import {
  DeliverSupportNotificationHandler,
  JoinSupportConversationHandler
} from './Infrastructure/ZLink/Actors/support-user-actor';
import { AllocateConversationHandler } from './Infrastructure/ZLink/Handlers/allocate-conversation-handler';
import { EnsureAgentConversationHandler } from './Infrastructure/ZLink/Handlers/ensure-agent-conversation-handler';
import { EnsureSupportUserActorHandler } from './Infrastructure/ZLink/Handlers/ensure-support-user-actor-handler';
import { ConversationIdleTimerHandler } from './Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-idle-timer-handler';
import {
  CloseConversationHandler,
  JoinConversationHandler,
  SendChatMessageHandler,
  SetTypingHandler
} from './Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-actor-handlers';
import {
  CloseConversationAtSpotHandler,
  JoinConversationAtSpotHandler,
  SendChatMessageAtSpotHandler,
  SetTypingAtSpotHandler
} from './Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation-operation-handlers';
import { ConversationSpot } from './Infrastructure/ZLink/Spots/ConversationSpot/conversation-spot';
import { SupportNotificationPublisher } from './Infrastructure/ZLink/Spots/ConversationSpot/Notifications/support-notification-publisher';
import {
  OpenConversationActorHandler,
  SetAgentAvailableHandler
} from './Infrastructure/ZLink/Spots/EntrySpot/support-entry-handlers';
import { SupportEntrySpot } from './Infrastructure/ZLink/Spots/EntrySpot/support-entry-spot';
import { SUPPORT_CHAT_CONFIG, createSupportChatConfigurationModule } from '../Configuration/sample-config';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function createSupportChatSupportModule() {
  class SupportChatSupportModule {}
  const configuration = createSupportChatConfigurationModule([
    'supportSpotEndpoint',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [SUPPORT_CHAT_CONFIG],
        useFactory: (config: SupportChatServerConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-support.log`)
            .traceLabel('support');
          builder.addLocationStore(createSupportChatLocationStore(config));
          supportChatLocationOptions(builder.configureLocations());
          const mesh = builder.addRouteMesh(SampleNames.conversationSpotMesh)
              .listen(config.supportSpotEndpoint).setRoutingIdPrefix('support-owner');
          const objectServer = mesh.objects().server();
          objectServer.addEntrySpot(SupportEntrySpot);
          objectServer.addSpotFactory(
            ConversationSpot.name,
            ConversationSpot,
            (factory) => factory.disableRelocation()
          );
          objectServer.addActorFactory(
            'support.user',
            SupportUserActorFactory,
            (factory) => factory.disableRelocation()
          );
          mesh.channelName(SampleNames.apiChannel).setWeight(0);
          mesh.channelName(SampleNames.supportChannel).addHandlerGroup('support');
          mesh.channelName(SampleNames.conversationSpotMesh);
          return builder.build();
        }
      })
    ],
    providers: [
      AgentAvailabilityDirectory,
      AgentAssignmentService,
      SupportConversationAllocator,
      SupportActorDirectory,
      SupportUserActorFactory,
      DeliverSupportNotificationHandler,
      JoinSupportConversationHandler,
      ConversationSpot,
      SupportEntrySpot,
      EnsureSupportUserActorHandler,
      EnsureAgentConversationHandler,
      AllocateConversationHandler,
      SetAgentAvailableHandler,
      OpenConversationActorHandler,
      JoinConversationHandler,
      SendChatMessageHandler,
      SetTypingHandler,
      CloseConversationHandler,
      CloseConversationAtSpotHandler,
      JoinConversationAtSpotHandler,
      SendChatMessageAtSpotHandler,
      SetTypingAtSpotHandler,
      ConversationIdleTimerHandler,
      SupportNotificationPublisher
    ]
  })(SupportChatSupportModule);

  return SupportChatSupportModule;
}

export { createSupportChatSupportModule };
