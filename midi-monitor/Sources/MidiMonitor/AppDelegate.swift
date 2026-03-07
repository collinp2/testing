import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
    private var statusBarController: StatusBarController!

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        statusBarController = StatusBarController()
        statusBarController.startPolling()
    }

    func applicationWillTerminate(_ notification: Notification) {
        statusBarController.stopPolling()
    }
}
