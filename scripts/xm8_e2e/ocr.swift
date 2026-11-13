// Local-only OCR for SDL surfaces; sends no screen data to a service.
import Foundation
import Vision
import AppKit
import CoreML
import CoreImage
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let image = NSImage(contentsOf: url)!
var rectangle = CGRect(origin: .zero, size: image.size)
var cg = image.cgImage(forProposedRect: &rectangle, context: nil, hints: nil)!
if CommandLine.arguments.dropFirst(2).contains("--high-contrast") {
    let input = CIImage(cgImage: cg)
    guard let filter = CIFilter(name: "CIColorControls") else {
        throw NSError(domain: "XM8ME2E", code: 4)
    }
    filter.setValue(input, forKey: kCIInputImageKey)
    filter.setValue(0, forKey: kCIInputSaturationKey)
    filter.setValue(8, forKey: kCIInputContrastKey)
    filter.setValue(-0.4, forKey: kCIInputBrightnessKey)
    guard let output = filter.outputImage,
          let processed = CIContext().createCGImage(output, from: input.extent) else {
        throw NSError(domain: "XM8ME2E", code: 5)
    }
    cg = processed
}
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
