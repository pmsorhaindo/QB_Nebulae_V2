// Minimal container app. Its only job is to carry the AUv3 extension onto the
// device; launch it once after installing so iOS registers the Audio Unit, then
// use the plug-in from AUM (or any other AUv3 host).
import SwiftUI

@main
struct CosmicFluxHostApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("CosmicFlux")
                    .font(.largeTitle.bold())
                Text("Cosmic delay and reverb, AUv3 audio effect")
                    .foregroundColor(.secondary)
                Divider()
                Text("The plug-in is installed. Open AUM, add an audio effect to a channel and choose \"Sorhaindo: CosmicFlux\" under Audio Unit Effects.")
                Text("Signal flow: dynamic flanger, Flux Chain (6-stage modulated series delay), dual barberpole phaser, Cosmos (8-line feedback delay network), mix and limiter.")
                Text("All controls are exposed as Audio Unit parameters, so AUM can map MIDI and record automation. Freeze holds the Cosmos network indefinitely.")
                Text("Nothing in this app processes audio; keep it installed so the extension stays available.")
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
            .padding()
        }
    }
}
