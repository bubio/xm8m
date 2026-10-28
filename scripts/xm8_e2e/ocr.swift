// Local-only OCR for SDL surfaces; sends no screen data to a service.
import Foundation
import Vision
import AppKit
import CoreML
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let image = NSImage(contentsOf: url)!
var rectangle = CGRect(origin: .zero, size: image.size)
let cg = image.cgImage(forProposedRect: &rectangle, context: nil, hints: nil)!
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.recognitionLanguages = ["en-US", "ja-JP"]
if CommandLine.arguments.dropFirst(2).contains("--english-cpu") {
    request.recognitionLanguages = ["en-US"]
    if #available(macOS 14.0, *) {
        let stages = try request.supportedComputeStageDevices
        guard !stages.isEmpty else { throw NSError(domain: "XM8ME2E", code: 1) }
        for (stage, devices) in stages {
            guard let cpu = devices.first(where: { if case .cpu = $0 { return true }; return false }) else {
                throw NSError(domain: "XM8ME2E", code: 2)
            }
            request.setComputeDevice(cpu, for: stage)
        }
    } else {
        throw NSError(domain: "XM8ME2E", code: 3)
    }
}
try VNImageRequestHandler(cgImage: cg).perform([request])
let results = (request.results ?? []).compactMap { item -> [String: Any]? in
    guard let candidate = item.topCandidates(1).first else { return nil }
    let box = item.boundingBox
    return ["text": candidate.string, "confidence": candidate.confidence,
            "x": box.midX * CGFloat(cg.width), "y": (1 - box.midY) * CGFloat(cg.height)]
}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject: results))
