import Foundation

struct ShellResult {
    let exitCode: Int32
    let output: String
}

struct ShellRunner {

    static func restartService(_ label: String) {
        let uid = getuid()
        let target = "gui/\(uid)/\(label)"
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/launchctl")
        process.arguments = ["kickstart", "-k", target]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        try? process.run()
    }

    static func runSync(_ executable: String, arguments: [String]) -> ShellResult {
        let process = Process()
        let pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            let output = String(data: data, encoding: .utf8) ?? ""
            return ShellResult(exitCode: process.terminationStatus, output: output)
        } catch {
            return ShellResult(exitCode: 1, output: "")
        }
    }
}
