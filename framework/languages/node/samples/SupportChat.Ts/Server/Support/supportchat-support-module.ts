import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { SupportNotificationDeliveryLog } from './notification-delivery-log';
import { SupportUserActorFactory } from './Infrastructure/ZLink/Actors/support-user-actor-factory';
import { ConversationEventMapper } from './Infrastructure/ZLink/Notifications/conversation-event-mapper';
import { SupportNotificationPublisher } from './Infrastructure/ZLink/Notifications/support-notification-publisher';
import { SupportEntrySpot } from './Infrastructure/ZLink/Spots/support-entry-spot';
import { ConversationSpot } from './Infrastructure/ZLink/Spots/conversation-spot';
import { AllocateConversationHandler } from './Infrastructure/ZLink/Handlers/allocate-conversation-handler';
import { AssignAgentHandler } from './Infrastructure/ZLink/Handlers/assign-agent-handler';
import { CloseConversationChannelHandler } from './Infrastructure/ZLink/Handlers/close-conversation-channel-handler';
import { EnsureSupportUserActorHandler } from './Infrastructure/ZLink/Handlers/ensure-support-user-actor-handler';
import { JoinConversationChannelHandler } from './Infrastructure/ZLink/Handlers/join-conversation-channel-handler';
import { OpenConversationChannelHandler } from './Infrastructure/ZLink/Handlers/open-conversation-channel-handler';
import { SendChatMessageChannelHandler } from './Infrastructure/ZLink/Handlers/send-chat-message-channel-handler';
import { SetAgentAvailableChannelHandler } from './Infrastructure/ZLink/Handlers/set-agent-available-channel-handler';
import { SetTypingChannelHandler } from './Infrastructure/ZLink/Handlers/set-typing-channel-handler';
import { SupportChatNotificationsHandler } from './Infrastructure/ZLink/Handlers/supportchat-notifications-handler';
import { CloseConversationHandler } from './Infrastructure/ZLink/Spots/Handlers/close-conversation-handler';
import { ConversationIdleTimerHandler } from './Infrastructure/ZLink/Spots/Handlers/conversation-idle-timer-handler';
import { OpenConversationActorHandler } from './Infrastructure/ZLink/Spots/Handlers/open-conversation-actor-handler';
import { SendChatMessageHandler } from './Infrastructure/ZLink/Spots/Handlers/send-chat-message-handler';
import { SetAgentAvailableHandler } from './Infrastructure/ZLink/Spots/Handlers/set-agent-available-handler';
import { SetTypingHandler } from './Infrastructure/ZLink/Spots/Handlers/set-typing-handler';
import {
  CONVERSATION_EXECUTOR,
  CONVERSATION_STARTER,
  SupportConversationAllocator
} from './Application/ConversationAssignment/support-conversation-allocator';
import { ZLinkConversationSpotAccess } from './Infrastructure/ZLink/conversation-spot-access';
import { AgentAvailabilityDirectory } from './Application/ConversationAssignment/agent-availability-directory';
import { AgentAssignmentService } from './Application/ConversationAssignment/agent-assignment-service';
import { SampleNames } from '../Configuration/sample-names';
function createSupportChatSupportModule(config: {
  registryRouterEndpoint: string;
  notificationEndpoint: string;
  supportEndpoint: string;
  apiEndpoint: string;
}) {
  class SupportChatSupportModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-support.log`)
            .traceNodeId('support');
          return builder
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.supportChannel)
            .enableServer(config.supportEndpoint)
            .addHandlerGroup('support')
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient()
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableServer(config.notificationEndpoint)
            .addHandlerGroup('notifications')
          .actorFactory(SampleNames.supportActorType, SupportUserActorFactory)
          .addSpotNode(SampleNames.conversationSpotType)
            .addEntrySpot(SupportEntrySpot)
            .addSpotFactory(ConversationSpot)
          .build();
        }
      })
    ],
    providers: [
      SupportNotificationDeliveryLog,
      SupportUserActorFactory,
      ConversationEventMapper,
      SupportNotificationPublisher,
      ZLinkConversationSpotAccess,
      { provide: CONVERSATION_STARTER, useExisting: ZLinkConversationSpotAccess },
      { provide: CONVERSATION_EXECUTOR, useExisting: ZLinkConversationSpotAccess },
      SupportConversationAllocator,
      AgentAvailabilityDirectory,
      AgentAssignmentService,
      SupportEntrySpot,
      AllocateConversationHandler,
      AssignAgentHandler,
      CloseConversationChannelHandler,
      EnsureSupportUserActorHandler,
      JoinConversationChannelHandler,
      OpenConversationChannelHandler,
      SendChatMessageChannelHandler,
      SetAgentAvailableChannelHandler,
      SetTypingChannelHandler,
      SupportChatNotificationsHandler,
      CloseConversationHandler,
      ConversationIdleTimerHandler,
      OpenConversationActorHandler,
      SendChatMessageHandler,
      SetAgentAvailableHandler,
      SetTypingHandler
    ]
  })(SupportChatSupportModule);

  return SupportChatSupportModule;
}

export { createSupportChatSupportModule };
