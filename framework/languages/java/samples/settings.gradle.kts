pluginManagement {
    plugins {
        id("org.jetbrains.kotlin.jvm") version "2.1.0"
        id("org.jetbrains.kotlin.plugin.spring") version "2.1.0"
    }
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

val zlinkGitHubPackagesUrl = providers.gradleProperty("zlink.githubPackagesUrl")
    .orElse(providers.environmentVariable("ZLINK_GITHUB_PACKAGES_URL"))
    .orElse("https://maven.pkg.github.com/kairos-code-dev/zlink")
val zlinkGitHubPackagesUser = providers.gradleProperty("zlink.githubPackagesUser")
    .orElse(providers.environmentVariable("MAVEN_REPOSITORY_USERNAME"))
    .orElse(providers.environmentVariable("GITHUB_ACTOR"))
val zlinkGitHubPackagesToken = providers.gradleProperty("zlink.githubPackagesToken")
    .orElse(providers.environmentVariable("MAVEN_REPOSITORY_PASSWORD"))
    .orElse(providers.environmentVariable("GITHUB_TOKEN"))

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        mavenCentral()
        val packageUser = zlinkGitHubPackagesUser.orNull
        val packageToken = zlinkGitHubPackagesToken.orNull
        if (!packageUser.isNullOrBlank() && !packageToken.isNullOrBlank()) {
            maven {
                name = "zlinkGitHubPackages"
                url = uri(zlinkGitHubPackagesUrl.get())
                credentials {
                    username = packageUser
                    password = packageToken
                }
            }
        }
    }
}

rootProject.name = "zlink-framework-java-samples"

includeBuild("..") {
    name = "zlink-framework-java-build"
}

val localBindingsDir = settingsDir.resolve("../../../../bindings/java").normalize()
val useLocalBindings = providers.gradleProperty("zlink.useLocalBindings")
    .map(String::toBoolean)
    .getOrElse(true)

if (useLocalBindings) {
    includeBuild(localBindingsDir) {
        name = "zlink-bindings-java"
        dependencySubstitution {
            substitute(module("systems.zlink:zlink")).using(project(":"))
        }
    }
}

include(
    ":java:GameQuest:Shared",
    ":java:GameQuest:Server:Configuration",
    ":java:GameQuest:Server:Registry",
    ":java:GameQuest:Server:GameApi",
    ":java:GameQuest:Server:QuestMission",
    ":java:GameQuest:Client",
    ":java:DeliveryDispatch:Shared",
    ":java:DeliveryDispatch:Server:Configuration",
    ":java:DeliveryDispatch:Server:Registry",
    ":java:DeliveryDispatch:Server:DispatchApi",
    ":java:DeliveryDispatch:Server:DispatchCenter",
    ":java:DeliveryDispatch:Server:Courier",
    ":java:DeliveryDispatch:Server:Tracking",
    ":java:DeliveryDispatch:Server:Session",
    ":java:DeliveryDispatch:Probe",
    ":java:DeliveryDispatch:Client",
    ":java:ShoppingMallCheckout:Shared",
    ":java:ShoppingMallCheckout:Server:Configuration",
    ":java:ShoppingMallCheckout:Server:Registry",
    ":java:ShoppingMallCheckout:Server:CommerceApi",
    ":java:ShoppingMallCheckout:Server:OrderWorkflow",
    ":java:ShoppingMallCheckout:Client",
    ":java:ShoppingMall:Shared",
    ":java:ShoppingMall:Server:Configuration",
    ":java:ShoppingMall:Server:Registry",
    ":java:ShoppingMall:Server:CommerceApi",
    ":java:ShoppingMall:Server:OrderWorkflow",
    ":java:ShoppingMall:Client",
    ":java:Bingo:Client",
    ":java:Bingo:Server:Api",
    ":java:Bingo:Server:Configuration",
    ":java:Bingo:Server:Play",
    ":java:Bingo:Server:Registry",
    ":java:Bingo:Server:Session",
    ":java:Bingo:Shared",
    ":java:SupportChat:Client",
    ":java:SupportChat:Server:Api",
    ":java:SupportChat:Server:Configuration",
    ":java:SupportChat:Server:Support",
    ":java:SupportChat:Server:Registry",
    ":java:SupportChat:Server:Session",
    ":java:SupportChat:Shared",
    ":java:TicTacToe:Client",
    ":java:TicTacToe:Server",
    ":java:TicTacToe:Shared",
    ":kotlin:Bingo:Client",
    ":kotlin:Bingo:Server:Api",
    ":kotlin:Bingo:Server:Configuration",
    ":kotlin:Bingo:Server:Play",
    ":kotlin:Bingo:Server:Registry",
    ":kotlin:Bingo:Server:Session",
    ":kotlin:Bingo:Shared",
    ":kotlin:GameQuest:Shared",
    ":kotlin:GameQuest:Server:Configuration",
    ":kotlin:GameQuest:Server:Registry",
    ":kotlin:GameQuest:Server:GameApi",
    ":kotlin:GameQuest:Server:QuestMission",
    ":kotlin:GameQuest:Client",
    ":kotlin:DeliveryDispatch:Shared",
    ":kotlin:DeliveryDispatch:Server:Configuration",
    ":kotlin:DeliveryDispatch:Server:Registry",
    ":kotlin:DeliveryDispatch:Server:DispatchApi",
    ":kotlin:DeliveryDispatch:Server:DispatchCenter",
    ":kotlin:DeliveryDispatch:Server:Courier",
    ":kotlin:DeliveryDispatch:Server:Tracking",
    ":kotlin:DeliveryDispatch:Server:Session",
    ":kotlin:DeliveryDispatch:Probe",
    ":kotlin:DeliveryDispatch:Client",
    ":kotlin:ShoppingMallCheckout:Shared",
    ":kotlin:ShoppingMallCheckout:Server:Configuration",
    ":kotlin:ShoppingMallCheckout:Server:Registry",
    ":kotlin:ShoppingMallCheckout:Server:CommerceApi",
    ":kotlin:ShoppingMallCheckout:Server:OrderWorkflow",
    ":kotlin:ShoppingMallCheckout:Client",
    ":kotlin:ShoppingMall:Shared",
    ":kotlin:ShoppingMall:Server:Configuration",
    ":kotlin:ShoppingMall:Server:Registry",
    ":kotlin:ShoppingMall:Server:CommerceApi",
    ":kotlin:ShoppingMall:Server:OrderWorkflow",
    ":kotlin:ShoppingMall:Client",
    ":kotlin:SupportChat:Client",
    ":kotlin:SupportChat:Server:Api",
    ":kotlin:SupportChat:Server:Configuration",
    ":kotlin:SupportChat:Server:Support",
    ":kotlin:SupportChat:Server:Registry",
    ":kotlin:SupportChat:Server:Session",
    ":kotlin:SupportChat:Shared",
    ":kotlin:TicTacToe:Client",
    ":kotlin:TicTacToe:Server",
    ":kotlin:TicTacToe:Shared",
)
