// 產生輸入法選單列圖示（範本圖，系統會依深淺色自動上色）。
// 用法：swift scripts/make-icon.swift <輸出.tiff>
import AppKit

let output = CommandLine.arguments.dropFirst().first ?? "icon.tiff"

func render(pixels: Int) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = NSSize(width: 16, height: 16)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let rect = NSRect(x: 0.5, y: 0.5, width: 15, height: 15)
    NSColor.black.setStroke()
    let frame = NSBezierPath(roundedRect: rect, xRadius: 3, yRadius: 3)
    frame.lineWidth = 1
    frame.stroke()
    let font = NSFont(name: "PingFangTC-Semibold", size: 11.5) ?? .boldSystemFont(ofSize: 11.5)
    let text = "棒" as NSString
    let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.black]
    let size = text.size(withAttributes: attrs)
    text.draw(at: NSPoint(x: (16 - size.width) / 2, y: (16 - size.height) / 2 + 0.3), withAttributes: attrs)
    NSGraphicsContext.restoreGraphicsState()
    return rep
}

let image = NSImage(size: NSSize(width: 16, height: 16))
image.addRepresentation(render(pixels: 16))
image.addRepresentation(render(pixels: 32))
guard let tiff = image.tiffRepresentation else { exit(1) }
try tiff.write(to: URL(fileURLWithPath: output))
print("已產生 \(output)")
