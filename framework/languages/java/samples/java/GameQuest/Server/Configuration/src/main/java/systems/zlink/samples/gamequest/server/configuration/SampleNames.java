package systems.zlink.samples.gamequest.server.configuration;

import java.time.Duration;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Channel, topic, discovery, node, and packet names for the GameQuest sample. */
public final class SampleNames {
    public static final String FanoutChannel = "gamequest.events";
    public static final String GameplayTopic = "gameplay.event";
    public static final String QuestSpotDiscovery = "gamequest.quest.spot";
    public static final String QuestSpotNode = "gamequest.quest.node";
    public static final String StreamNode = "gamequest.stream";

    public static final String SubscribePacket = "SubscribeQuestReq";
    public static final String ProgressPacket = "QuestProgressNotify";
    public static final String CompletedPacket = "QuestCompletedNotify";

    public static final Duration RequestTimeout = Duration.ofSeconds(10);

    /** Client-facing action channel hosted by a GameApi instance ({@code api-a}/{@code api-b}). */
    public static String gameApiActionChannel(String apiName) {
        return "gamequest.gameapi." + apiName;
    }

    /** Channel a QuestMission instance hosts for snapshot/notify exchange with GameApi. */
    public static String questMissionChannel(String missionName) {
        return "gamequest.mission." + missionName;
    }

    private SampleNames() {
    }
}
