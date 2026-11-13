// Compare a calibrated, fixed screen region. No network or external dependencies.
import AppKit
import Foundation

func load(_ path: String) throws -> CGImage {
    guard let image = NSImage(contentsOfFile: path),
          let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
        throw NSError(domain: "Invalid PNG", code: 1)
    }
    return cg
}
func pixels(_ image: CGImage) throws -> [UInt8] {
    var bytes = [UInt8](repeating: 0, count: image.width * image.height * 4)
    let valid = bytes.withUnsafeMutableBytes { buffer -> Bool in
        guard let context = CGContext(data: buffer.baseAddress, width: image.width,
              height: image.height, bitsPerComponent: 8, bytesPerRow: image.width * 4,
              space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        return true
    }
    if !valid { throw NSError(domain: "Cannot decode PNG", code: 2) }
    return bytes
}
let args = CommandLine.arguments
guard args.count == 7, let x0 = Int(args[3]), let y0 = Int(args[4]),
      let x1 = Int(args[5]), let y1 = Int(args[6]) else { exit(64) }
let actual = try load(args[1]), expected = try load(args[2])
guard actual.width == expected.width, actual.height == expected.height,
      x0 >= 0, y0 >= 0, x1 > x0, y1 > y0, x1 <= actual.width, y1 <= actual.height else { exit(65) }
let a = try pixels(actual), b = try pixels(expected)
var changed = 0, totalDifference = 0, referenceLow = 255, referenceHigh = 0, actualLow = 255, actualHigh = 0
for y in y0..<y1 {
    for x in x0..<x1 {
        let offset = (y * actual.width + x) * 4
        var largest = 0
        for c in 0..<3 {
            let difference = abs(Int(a[offset+c]) - Int(b[offset+c]))
            largest = max(largest, difference)
            totalDifference += difference
            referenceLow = min(referenceLow, Int(b[offset+c]))
            referenceHigh = max(referenceHigh, Int(b[offset+c]))
            actualLow = min(actualLow, Int(a[offset+c]))
            actualHigh = max(actualHigh, Int(a[offset+c]))
        }
        if largest > 32 { changed += 1 }
    }
}
let count = (x1-x0) * (y1-y0)
let value: [String: Any] = ["changed_fraction": Double(changed) / Double(count),
    "mean_difference": Double(totalDifference) / Double(count * 3 * 255), "pixels":count,
    "reference_contrast": referenceHigh - referenceLow, "actual_contrast":actualHigh - actualLow]
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject: value))
