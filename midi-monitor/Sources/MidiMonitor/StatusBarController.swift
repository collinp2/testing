import AppKit

class StatusBarController {
    private var statusItem: NSStatusItem
    private var serverStatus: ServiceStatus = .unknown
    private var tunnelStatus: ServiceStatus = .unknown
    private let monitor = ServiceMonitor()
    private var pollTimer: Timer?
    private var serverMenuItem: NSMenuItem!
    private var tunnelMenuItem: NSMenuItem!

    init() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        buildMenu()
        updateIcon()
    }

    func startPolling() {
        poll()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            self?.poll()
        }
    }

    func stopPolling() {
        pollTimer?.invalidate()
    }

    private func poll() {
        monitor.checkServer { [weak self] status in
            DispatchQueue.main.async {
                self?.serverStatus = status
                self?.updateIcon()
                self?.updateMenuTitles()
            }
        }
        monitor.checkTunnel { [weak self] status in
            DispatchQueue.main.async {
                self?.tunnelStatus = status
                self?.updateIcon()
                self?.updateMenuTitles()
            }
        }
    }

    private func buildMenu() {
        let menu = NSMenu()

        serverMenuItem = NSMenuItem(title: "Server: checking...", action: nil, keyEquivalent: "")
        serverMenuItem.isEnabled = false
        tunnelMenuItem = NSMenuItem(title: "Tunnel: checking...", action: nil, keyEquivalent: "")
        tunnelMenuItem.isEnabled = false

        menu.addItem(serverMenuItem)
        menu.addItem(tunnelMenuItem)
        menu.addItem(.separator())

        let restartServer = NSMenuItem(title: "Restart Server", action: #selector(restartServer), keyEquivalent: "")
        restartServer.target = self
        menu.addItem(restartServer)

        let restartTunnel = NSMenuItem(title: "Restart Tunnel", action: #selector(restartTunnel), keyEquivalent: "")
        restartTunnel.target = self
        menu.addItem(restartTunnel)

        menu.addItem(.separator())

        let serverLogs = NSMenuItem(title: "View Server Logs", action: #selector(viewServerLogs), keyEquivalent: "")
        serverLogs.target = self
        menu.addItem(serverLogs)

        let tunnelLogs = NSMenuItem(title: "View Tunnel Logs", action: #selector(viewTunnelLogs), keyEquivalent: "")
        tunnelLogs.target = self
        menu.addItem(tunnelLogs)

        menu.addItem(.separator())

        let quit = NSMenuItem(title: "Quit MidiMonitor", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        menu.addItem(quit)

        statusItem.menu = menu
    }

    private func updateMenuTitles() {
        serverMenuItem.title = "Server: \(label(serverStatus))"
        tunnelMenuItem.title = "Tunnel: \(label(tunnelStatus))"
    }

    private func label(_ status: ServiceStatus) -> String {
        switch status {
        case .up:      return "Running ●"
        case .down:    return "Stopped ●"
        case .unknown: return "Checking..."
        }
    }

    private func updateIcon() {
        guard let button = statusItem.button else { return }
        button.image = makeStatusImage()
        button.image?.isTemplate = false
    }

    private func makeStatusImage() -> NSImage {
        let size = NSSize(width: 26, height: 18)
        let image = NSImage(size: size)
        image.lockFocus()
        let dotSize: CGFloat = 8
        let y = (size.height - dotSize) / 2
        colorFor(serverStatus).setFill()
        NSBezierPath(ovalIn: NSRect(x: 2, y: y, width: dotSize, height: dotSize)).fill()
        colorFor(tunnelStatus).setFill()
        NSBezierPath(ovalIn: NSRect(x: 16, y: y, width: dotSize, height: dotSize)).fill()
        image.unlockFocus()
        return image
    }

    private func colorFor(_ status: ServiceStatus) -> NSColor {
        switch status {
        case .up:      return .systemGreen
        case .down:    return .systemRed
        case .unknown: return .systemGray
        }
    }

    @objc private func restartServer() {
        ShellRunner.restartService("com.collinpeterson.midi-voice-command")
        DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in self?.poll() }
    }

    @objc private func restartTunnel() {
        ShellRunner.restartService("com.collinpeterson.cloudflared")
        DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in self?.poll() }
    }

    @objc private func viewServerLogs() {
        NSWorkspace.shared.open(URL(fileURLWithPath: NSHomeDirectory() + "/Library/Logs/midi-voice-command.log"))
    }

    @objc private func viewTunnelLogs() {
        NSWorkspace.shared.open(URL(fileURLWithPath: NSHomeDirectory() + "/Library/Logs/cloudflared.log"))
    }
}
