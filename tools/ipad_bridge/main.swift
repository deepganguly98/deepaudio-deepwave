// DeepWave iPad bridge: serves a multi-touch playing page over Wi-Fi and turns the page's MIDI
// (sent over a WebSocket) into a virtual CoreMIDI source called "DeepWave iPad".
// No dependencies beyond macOS system frameworks.
//   build: swiftc -O -suppress-warnings main.swift -o ipad_bridge
//   run:   ./ipad_bridge [--port 8080] [--html index.html] [--log] [--loopback]
import Foundation
import Network
import CoreMIDI
import CryptoKit

setvbuf(stdout, nil, _IOLBF, 0)
let args = CommandLine.arguments
func argValue (_ name: String) -> String? { if let i = args.firstIndex (of: name), i + 1 < args.count { return args[i + 1] }; return nil }
let port = UInt16 (argValue ("--port") ?? "8080") ?? 8080
let htmlPath = argValue ("--html") ?? "index.html"
let verbose = args.contains ("--log")
let loopback = args.contains ("--loopback")   // self-test: count what we emit

// ---- CoreMIDI virtual source ------------------------------------------------------------
var client = MIDIClientRef()
MIDIClientCreate ("DeepWave iPad Bridge" as CFString, nil, nil, &client)
var source = MIDIEndpointRef()
MIDISourceCreate (client, "DeepWave iPad" as CFString, &source)

var loopBytes = 0
if loopback
{
    var inPort = MIDIPortRef()
    MIDIInputPortCreateWithBlock (client, "loopback" as CFString, &inPort) { listPtr, _ in
        let n = Int (listPtr.pointee.numPackets)
        var pkt = listPtr.pointee.packet
        for _ in 0..<n { loopBytes += Int (pkt.length); pkt = MIDIPacketNext (&pkt).pointee }
    }
    MIDIPortConnectSource (inPort, source, nil)
}

func sendMIDI (_ bytes: [UInt8])
{
    let size = 1024
    let raw = UnsafeMutableRawPointer.allocate (byteCount: size, alignment: 4)
    defer { raw.deallocate() }
    let list = raw.bindMemory (to: MIDIPacketList.self, capacity: 1)
    let pkt = MIDIPacketListInit (list)
    _ = MIDIPacketListAdd (list, size, pkt, 0, bytes.count, bytes)
    MIDIReceived (source, list)
    if verbose { print ("midi", bytes.map { String (format: "%02x", $0) }.joined (separator: " ")) }
}

// ---- tiny HTTP + WebSocket server -------------------------------------------------------
let wsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

final class Connection
{
    let c: NWConnection
    var buf = [UInt8]()
    var upgraded = false
    var sounding = Set<UInt16>()      // (channel << 8 | note): so a dropped client can't leave stuck notes

    init (_ c: NWConnection) { self.c = c }

    func start() { c.start (queue: .main); receive() }

    func receive()
    {
        c.receive (minimumIncompleteLength: 1, maximumLength: 65536) { [self] data, _, done, err in
            if let d = data, !d.isEmpty { buf.append (contentsOf: d); upgraded ? frames() : http() }
            if done || err != nil { close(); return }
            receive()
        }
    }

    func close()
    {
        for k in sounding { sendMIDI ([0x80 | UInt8 ((k >> 8) & 0x0F), UInt8 (k & 0x7F), 0]) }
        sounding.removeAll()
        if upgraded { print ("client disconnected") }
        c.cancel()
    }

    func respond (_ status: String, type: String, body: Data)
    {
        var head = "HTTP/1.1 \(status)\r\nContent-Type: \(type)\r\nContent-Length: \(body.count)\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"
        head += ""
        var out = Data (head.utf8); out.append (body)
        c.send (content: out, contentContext: .finalMessage, isComplete: true, completion: .contentProcessed { [self] _ in c.cancel() })
    }

    func http()
    {
        guard let end = findHeaderEnd() else { return }
        let headText = String (decoding: buf[0..<end], as: UTF8.self)
        buf.removeFirst (end + 4)
        var lines = headText.components (separatedBy: "\r\n")
        let reqLine = lines.removeFirst().split (separator: " ")
        var h = [String: String]()
        for l in lines { if let i = l.firstIndex (of: ":") { h[l[..<i].lowercased()] = l[l.index (after: i)...].trimmingCharacters (in: .whitespaces) } }
        let path = reqLine.count > 1 ? String (reqLine[1]) : "/"

        if let key = h["sec-websocket-key"], h["upgrade"]?.lowercased() == "websocket"
        {
            let accept = Data (Insecure.SHA1.hash (data: Data ((key + wsGuid).utf8))).base64EncodedString()
            let resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: \(accept)\r\n\r\n"
            c.send (content: Data (resp.utf8), completion: .contentProcessed { _ in })
            upgraded = true
            print ("client connected: \(c.endpoint)")
            if !buf.isEmpty { frames() }
            return
        }
        if path == "/" || path.hasPrefix ("/index")
        {
            if let d = FileManager.default.contents (atPath: htmlPath) { respond ("200 OK", type: "text/html; charset=utf-8", body: d) }
            else { respond ("500 Internal Server Error", type: "text/plain", body: Data ("index.html not found at \(htmlPath)".utf8)) }
        }
        else { respond ("404 Not Found", type: "text/plain", body: Data ("not found".utf8)) }
    }

    func findHeaderEnd() -> Int?
    {
        if buf.count < 4 { return nil }
        for i in 0...(buf.count - 4) where buf[i] == 13 && buf[i + 1] == 10 && buf[i + 2] == 13 && buf[i + 3] == 10 { return i }
        return nil
    }

    func frames()
    {
        while buf.count >= 2
        {
            let b0 = buf[0], b1 = buf[1]
            let opcode = b0 & 0x0F, masked = (b1 & 0x80) != 0
            var len = Int (b1 & 0x7F), idx = 2
            if len == 126 { guard buf.count >= 4 else { return }; len = Int (buf[2]) << 8 | Int (buf[3]); idx = 4 }
            else if len == 127 { guard buf.count >= 10 else { return }; len = 0; for k in 0..<8 { len = len << 8 | Int (buf[2 + k]) }; idx = 10 }
            let maskLen = masked ? 4 : 0
            guard buf.count >= idx + maskLen + len else { return }
            var payload = Array (buf[(idx + maskLen)..<(idx + maskLen + len)])
            if masked { let m = Array (buf[idx..<(idx + 4)]); for k in 0..<payload.count { payload[k] ^= m[k & 3] } }
            buf.removeFirst (idx + maskLen + len)

            switch opcode
            {
                case 0x0, 0x1, 0x2: forward (payload)
                case 0x8: close(); return
                case 0x9: c.send (content: Data ([0x8A, UInt8 (min (payload.count, 125))] + payload.prefix (125)), completion: .contentProcessed { _ in })
                default: break
            }
        }
    }

    // payload = one or more complete channel-voice messages
    func forward (_ p: [UInt8])
    {
        var i = 0
        while i < p.count
        {
            let s = p[i]
            guard s >= 0x80, s < 0xF0 else { i += 1; continue }
            let kind = s & 0xF0, n = (kind == 0xC0 || kind == 0xD0) ? 2 : 3
            guard i + n <= p.count else { break }
            let msg = Array (p[i..<(i + n)])
            if kind == 0x90 && msg[2] > 0 { sounding.insert (UInt16 (s & 0x0F) << 8 | UInt16 (msg[1])) }
            else if kind == 0x80 || (kind == 0x90 && msg[2] == 0) { sounding.remove (UInt16 (s & 0x0F) << 8 | UInt16 (msg[1])) }
            sendMIDI (msg)
            i += n
        }
    }
}

func lanAddresses() -> [String]
{
    var out = [String]()
    var ifa: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs (&ifa) == 0, let first = ifa else { return out }
    defer { freeifaddrs (ifa) }
    var p = first
    while true
    {
        let f = p.pointee
        if f.ifa_addr.pointee.sa_family == UInt8 (AF_INET), (f.ifa_flags & UInt32 (IFF_LOOPBACK)) == 0
        {
            var host = [CChar] (repeating: 0, count: Int (NI_MAXHOST))
            getnameinfo (f.ifa_addr, socklen_t (f.ifa_addr.pointee.sa_len), &host, socklen_t (host.count), nil, 0, NI_NUMERICHOST)
            out.append (String (cString: host))
        }
        guard let next = f.ifa_next else { break }
        p = next
    }
    return out
}

let listener: NWListener
do { listener = try NWListener (using: .tcp, on: NWEndpoint.Port (rawValue: port)!) }
catch { print ("cannot listen on port \(port): \(error)"); exit (1) }
listener.newConnectionHandler = { Connection ($0).start() }
listener.start (queue: .main)

print ("DeepWave iPad bridge")
print ("  virtual MIDI source: \"DeepWave iPad\"  (enable it in DeepWave -> Options -> Audio/MIDI Settings)")
for ip in lanAddresses() { print ("  open on the iPad (same Wi-Fi):  http://\(ip):\(port)") }
print ("  host name:  http://\(ProcessInfo.processInfo.hostName):\(port)")
if loopback
{
    Timer.scheduledTimer (withTimeInterval: 1.0, repeats: true) { _ in print ("loopback bytes seen on the virtual port: \(loopBytes)") }
}
dispatchMain()
