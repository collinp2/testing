// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "MidiMonitor",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "MidiMonitor",
            path: "Sources/MidiMonitor",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("Foundation")
            ]
        )
    ]
)
