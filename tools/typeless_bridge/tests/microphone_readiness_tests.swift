import Foundation

@main
enum MicrophoneReadinessTests {
    static func main() {
        let disabled = MicrophoneReadinessSnapshot(enabled: false, hasControl: false,
            audioSubscribed: false, statsSubscribed: false, armed: false,
            outputHealthy: false, outputRouteValid: false)
        precondition(disabled.ready)

        let audioOnly = MicrophoneReadinessSnapshot(enabled: true, hasControl: true,
            audioSubscribed: true, statsSubscribed: false, armed: false,
            outputHealthy: true, outputRouteValid: true)
        precondition(!audioOnly.ready)

        let notArmed = MicrophoneReadinessSnapshot(enabled: true, hasControl: true,
            audioSubscribed: true, statsSubscribed: true, armed: false,
            outputHealthy: true, outputRouteValid: true)
        precondition(!notArmed.ready)

        let wrongRoute = MicrophoneReadinessSnapshot(enabled: true, hasControl: true,
            audioSubscribed: true, statsSubscribed: true, armed: true,
            outputHealthy: true, outputRouteValid: false)
        precondition(!wrongRoute.ready)

        let ready = MicrophoneReadinessSnapshot(enabled: true, hasControl: true,
            audioSubscribed: true, statsSubscribed: true, armed: true,
            outputHealthy: true, outputRouteValid: true)
        precondition(ready.ready)

        let diagnosticsStillSubscribing = MicrophoneReadinessSnapshot(enabled: true, hasControl: true,
            audioSubscribed: true, statsSubscribed: false, armed: true,
            outputHealthy: true, outputRouteValid: true)
        precondition(diagnosticsStillSubscribing.ready)
        print("PASS: microphone readiness requires audio, arm acknowledgement and verified output")
    }
}
