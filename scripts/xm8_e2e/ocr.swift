// Local-only OCR for SDL surfaces; sends no screen data to a service.
import Foundation
import Vision
import AppKit
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let image = NSImage(contentsOf: url)!
var rectangle = CGRect(origin: .zero, size: image.size)
let cg = image.cgImage(forProposedRect: &rectangle, context: nil, hints: nil)!
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.recognitionLanguages = ["en-US", "ja-JP"]
try VNImageRequestHandler(cgImage: cg).perform([request])
let results = (request.results ?? []).compactMap { item -> [String: Any]? in
    guard let candidate = item.topCandidates(1).first else { return nil }
    let box = item.boundingBox
    return ["text": candidate.string, "confidence": candidate.confidence,
            "x": box.midX * CGFloat(cg.width), "y": (1 - box.midY) * CGFloat(cg.height)]
}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject: results))
