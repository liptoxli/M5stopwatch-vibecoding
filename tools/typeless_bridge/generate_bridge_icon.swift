import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let outputDir = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ".")

struct Color {
    let r: CGFloat
    let g: CGFloat
    let b: CGFloat
    let a: CGFloat

    var cg: CGColor {
        CGColor(red: r, green: g, blue: b, alpha: a)
    }
}

let background = Color(r: 0.03, g: 0.05, b: 0.08, a: 1)
let outline = Color(r: 0.70, g: 0.86, b: 1.0, a: 0.92)
let screenBlue = Color(r: 0.18, g: 0.75, b: 1.0, a: 0.95)
let screenPurple = Color(r: 0.56, g: 0.45, b: 1.0, a: 0.96)
let buttonBlue = Color(r: 0.15, g: 0.52, b: 1.0, a: 0.95)

func drawIcon(size: Int, scale: CGFloat) throws -> CGImage {
    let pixels = Int(CGFloat(size) * scale)
    var data = [UInt8](repeating: 0, count: pixels * pixels * 4)
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    guard let context = CGContext(data: &data,
                                  width: pixels,
                                  height: pixels,
                                  bitsPerComponent: 8,
                                  bytesPerRow: pixels * 4,
                                  space: colorSpace,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
        throw NSError(domain: "bridge-icon", code: 1)
    }

    context.scaleBy(x: scale, y: scale)
    context.setAllowsAntialiasing(true)
    context.setShouldAntialias(true)

    let canvas = CGRect(x: 0, y: 0, width: CGFloat(size), height: CGFloat(size))
    context.clear(canvas)

    let bgRect = canvas.insetBy(dx: CGFloat(size) * 0.08, dy: CGFloat(size) * 0.08)
    context.addPath(CGPath(roundedRect: bgRect, cornerWidth: CGFloat(size) * 0.22, cornerHeight: CGFloat(size) * 0.22, transform: nil))
    context.setFillColor(background.cg)
    context.fillPath()

    context.setShadow(offset: .zero, blur: CGFloat(size) * 0.045, color: screenBlue.cg.copy(alpha: 0.45))
    let body = CGRect(x: CGFloat(size) * 0.19,
                      y: CGFloat(size) * 0.17,
                      width: CGFloat(size) * 0.62,
                      height: CGFloat(size) * 0.66)
    context.addEllipse(in: body)
    context.setStrokeColor(outline.cg)
    context.setLineWidth(CGFloat(size) * 0.038)
    context.strokePath()

    context.setShadow(offset: .zero, blur: CGFloat(size) * 0.03, color: nil)

    let leftButton = CGRect(x: CGFloat(size) * 0.11,
                            y: CGFloat(size) * 0.42,
                            width: CGFloat(size) * 0.085,
                            height: CGFloat(size) * 0.18)
    let rightButton = CGRect(x: CGFloat(size) * 0.805,
                             y: CGFloat(size) * 0.42,
                             width: CGFloat(size) * 0.085,
                             height: CGFloat(size) * 0.18)
    for rect in [leftButton, rightButton] {
        context.addPath(CGPath(roundedRect: rect, cornerWidth: CGFloat(size) * 0.035, cornerHeight: CGFloat(size) * 0.035, transform: nil))
        context.setFillColor(buttonBlue.cg)
        context.fillPath()
    }

    let screen = CGRect(x: CGFloat(size) * 0.35,
                        y: CGFloat(size) * 0.35,
                        width: CGFloat(size) * 0.30,
                        height: CGFloat(size) * 0.30)
    context.setShadow(offset: .zero, blur: CGFloat(size) * 0.055, color: screenPurple.cg.copy(alpha: 0.65))
    context.addEllipse(in: screen)
    context.setFillColor(screenPurple.cg)
    context.fillPath()

    context.setShadow(offset: .zero, blur: CGFloat(size) * 0.025, color: screenBlue.cg.copy(alpha: 0.55))
    context.setStrokeColor(screenBlue.cg)
    context.setLineWidth(CGFloat(size) * 0.025)
    let chevron = CGMutablePath()
    chevron.move(to: CGPoint(x: CGFloat(size) * 0.43, y: CGFloat(size) * 0.53))
    chevron.addLine(to: CGPoint(x: CGFloat(size) * 0.48, y: CGFloat(size) * 0.50))
    chevron.addLine(to: CGPoint(x: CGFloat(size) * 0.43, y: CGFloat(size) * 0.47))
    context.addPath(chevron)
    context.strokePath()

    context.move(to: CGPoint(x: CGFloat(size) * 0.52, y: CGFloat(size) * 0.46))
    context.addLine(to: CGPoint(x: CGFloat(size) * 0.58, y: CGFloat(size) * 0.46))
    context.strokePath()

    guard let image = context.makeImage() else {
        throw NSError(domain: "bridge-icon", code: 2)
    }
    return image
}

func writePNG(_ image: CGImage, to url: URL) throws {
    guard let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
        throw NSError(domain: "bridge-icon", code: 3)
    }
    CGImageDestinationAddImage(destination, image, nil)
    if !CGImageDestinationFinalize(destination) {
        throw NSError(domain: "bridge-icon", code: 4)
    }
}

try FileManager.default.createDirectory(at: outputDir, withIntermediateDirectories: true)

for size in [16, 32, 128, 256, 512] {
    try writePNG(drawIcon(size: size, scale: 1), to: outputDir.appendingPathComponent("icon_\(size)x\(size).png"))
    try writePNG(drawIcon(size: size, scale: 2), to: outputDir.appendingPathComponent("icon_\(size)x\(size)@2x.png"))
}

print(outputDir.path)
