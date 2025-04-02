import Foundation
import Network


class MDNSAdvertiser: ObservableObject {
    @Published var serviceName: String = ""
    private var listener: NWListener?

    func startAdvertising(serviceName: String = "MyClipboardService",
                         serviceType: String = "_clipboard._tcp",
                         port: UInt16) {
        print("MDNSAdvertiser will start")
        do {
            
            let parameters = NWParameters.tcp
            parameters.includePeerToPeer = true
            
            listener = try NWListener(using: parameters, on: NWEndpoint.Port(integerLiteral: port))
            listener?.service = NWListener.Service(name: serviceName,
                                                 type: serviceType)
            listener?.serviceRegistrationUpdateHandler = { change in
                DispatchQueue.main.async(execute: DispatchWorkItem(block: {
                    print("Service registration updated: \(change)")
                    if case .add(let endpoint) = change {
                        if case .service(let name, _, _, _) = endpoint {
                            self.serviceName = name
                        }
                    }
                }))
            }
            
            listener?.stateUpdateHandler = { state in
                DispatchQueue.main.async {
                    print("Listener state updated: \(state)")
                }
            }
            listener?.newConnectionHandler = { connection in
                print("New connection received: \(connection)")
                // Handle connections as needed
                connection.cancel()
            }
            listener?.start(queue: .main)
        } catch {
            print("Failed to start listener: \(error)")
        }
    }

    func stopAdvertising() {
        print("MDNSAdvertiser will stop")
        listener?.cancel()
        listener = nil
    }
}
