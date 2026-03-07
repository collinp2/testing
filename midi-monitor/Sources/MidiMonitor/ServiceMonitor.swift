import Foundation

enum ServiceStatus {
    case up, down, unknown
}

class ServiceMonitor {
    private let session: URLSession = {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 3.0
        config.timeoutIntervalForResource = 3.0
        config.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: config)
    }()

    func checkServer(completion: @escaping (ServiceStatus) -> Void) {
        guard let url = URL(string: "http://localhost:3000/commands") else {
            completion(.down); return
        }
        session.dataTask(with: url) { _, response, _ in
            if let http = response as? HTTPURLResponse, http.statusCode == 200 {
                completion(.up)
            } else {
                completion(.down)
            }
        }.resume()
    }

    func checkTunnel(completion: @escaping (ServiceStatus) -> Void) {
        guard let url = URL(string: "http://127.0.0.1:20241/ready") else {
            checkTunnelViaPgrep(completion: completion); return
        }
        session.dataTask(with: url) { [weak self] _, response, error in
            if let http = response as? HTTPURLResponse, http.statusCode == 200 {
                completion(.up)
            } else if error != nil {
                self?.checkTunnelViaPgrep(completion: completion)
            } else {
                completion(.down)
            }
        }.resume()
    }

    private func checkTunnelViaPgrep(completion: @escaping (ServiceStatus) -> Void) {
        let result = ShellRunner.runSync("/usr/bin/pgrep", arguments: ["-x", "cloudflared"])
        completion(result.exitCode == 0 ? .up : .down)
    }
}
