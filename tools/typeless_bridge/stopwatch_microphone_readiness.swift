import Foundation

struct MicrophoneReadinessSnapshot: Equatable {
    var enabled: Bool
    var hasControl: Bool
    var audioSubscribed: Bool
    var statsSubscribed: Bool
    var armed: Bool
    var outputHealthy: Bool
    var outputRouteValid: Bool

    var ready: Bool {
        !enabled || (hasControl && audioSubscribed && armed &&
                     outputHealthy && outputRouteValid)
    }
}
