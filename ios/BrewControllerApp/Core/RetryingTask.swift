// RetryingTask: a loading state for a screen. Runs an async load, keeps the last value, and retries
// a failed or not-ready load with a growing pause (the controller needs a moment after launch).
import Observation

enum LoadPhase<Value: Sendable>: Sendable {
    case idle
    case loading
    case loaded(Value)
    case failed(BrewAPIError)

    var value: Value? {
        if case .loaded(let value) = self { return value }
        return nil
    }
    var isLoading: Bool {
        if case .loading = self { return true }
        return false
    }
    var error: BrewAPIError? {
        if case .failed(let error) = self { return error }
        return nil
    }
}

@MainActor
@Observable
final class RetryingTask<Value: Sendable> {
    private(set) var phase: LoadPhase<Value> = .idle
    /// The last good value, kept while a refresh runs or fails.
    private(set) var lastValue: Value?

    @ObservationIgnored private let operation: @Sendable () async throws -> Value
    @ObservationIgnored private let maxAttempts: Int
    @ObservationIgnored private let firstDelay: Duration
    @ObservationIgnored private var task: Task<Void, Never>?

    /// - maxAttempts: tries before giving up (a failure that retrying cannot fix, such as a 409 or a
    ///   decoding error, stops at once).
    init(maxAttempts: Int = 4, firstDelay: Duration = .milliseconds(400),
         operation: @escaping @Sendable () async throws -> Value) {
        self.maxAttempts = max(1, maxAttempts)
        self.firstDelay = firstDelay
        self.operation = operation
    }

    /// Starts (or restarts) the load.
    func load() {
        task?.cancel()
        phase = .loading
        task = Task { [weak self] in
            await self?.run()
        }
    }

    /// Cancels a load in progress.
    func cancel() {
        task?.cancel()
        task = nil
        if phase.isLoading { phase = lastValue.map { .loaded($0) } ?? .idle }
    }

    /// The load, awaited (for `.task { await model.run() }`).
    func run() async {
        phase = .loading
        var delay = firstDelay
        var attempt = 0
        while !Task.isCancelled {
            attempt += 1
            do {
                let value = try await operation()
                lastValue = value
                phase = .loaded(value)
                return
            } catch let error as BrewAPIError {
                if attempt >= maxAttempts || !RetryingTask.isRetryable(error) {
                    phase = .failed(error)
                    return
                }
            } catch is CancellationError {
                return
            } catch {
                phase = .failed(.invalidRequest(String(describing: error)))
                return
            }
            try? await Task.sleep(for: delay)
            delay *= 2
        }
    }

    /// Not ready, timeouts and server-side 5xx are worth another try; the rest are not.
    static func isRetryable(_ error: BrewAPIError) -> Bool {
        switch error {
        case .notReady, .timeout: return true
        case .http(let status, _): return status >= 500
        case .rejected, .decoding, .invalidRequest: return false
        }
    }
}
