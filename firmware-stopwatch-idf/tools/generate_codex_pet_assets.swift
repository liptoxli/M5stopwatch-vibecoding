import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

struct RGBA {
    let r: CGFloat
    let g: CGFloat
    let b: CGFloat
    let a: CGFloat

    var cgColor: CGColor {
        CGColor(red: r, green: g, blue: b, alpha: a)
    }
}

struct FrameSpec {
    let name: String
    let pose: Pose
    let y: CGFloat
    let squash: CGFloat
    let tilt: CGFloat
    let eye: Eye
    let glow: CGFloat
    let blush: CGFloat
    let processingPhase: Int
}

enum Pose {
    case idle
    case touch
    case processing
}

enum Eye {
    case codex
    case blink
    case happy
    case surprised
}

let width = 172
let height = 172
let outputDir = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    .appendingPathComponent("main/assets/images")
let previewURL = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    .appendingPathComponent("build/codex_pet_preview.png")

let cyan = RGBA(r: 0.27, g: 0.86, b: 1.0, a: 1.0)
let violet = RGBA(r: 0.55, g: 0.68, b: 1.0, a: 1.0)
let soft = RGBA(r: 0.86, g: 0.97, b: 1.0, a: 1.0)
let shellTop = RGBA(r: 0.07, g: 0.17, b: 0.30, a: 1.0)
let shellBottom = RGBA(r: 0.015, g: 0.045, b: 0.09, a: 1.0)
let screenTop = RGBA(r: 0.035, g: 0.15, b: 0.26, a: 0.96)
let screenBottom = RGBA(r: 0.01, g: 0.035, b: 0.07, a: 0.98)

let frames: [FrameSpec] = [
    FrameSpec(name: "codex_pet_idle", pose: .idle, y: 1, squash: 1.00, tilt: 0, eye: .codex, glow: 0.50, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_idle0", pose: .idle, y: 1, squash: 1.00, tilt: 0, eye: .codex, glow: 0.50, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_idle1", pose: .idle, y: -1, squash: 1.01, tilt: -2, eye: .codex, glow: 0.58, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_idle2", pose: .idle, y: 0, squash: 1.00, tilt: 2, eye: .codex, glow: 0.54, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_idle3", pose: .idle, y: -2, squash: 0.99, tilt: -4, eye: .surprised, glow: 0.62, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_idle4", pose: .idle, y: 2, squash: 1.02, tilt: 3, eye: .happy, glow: 0.55, blush: 0.08, processingPhase: 0),
    FrameSpec(name: "codex_pet_idle5", pose: .idle, y: -3, squash: 0.98, tilt: 0, eye: .codex, glow: 0.66, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_blink0", pose: .idle, y: 0, squash: 1.00, tilt: 0, eye: .blink, glow: 0.50, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_blink1", pose: .idle, y: 1, squash: 1.00, tilt: 0, eye: .blink, glow: 0.42, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_touch0", pose: .touch, y: 5, squash: 0.93, tilt: -3, eye: .surprised, glow: 0.72, blush: 0.25, processingPhase: 0),
    FrameSpec(name: "codex_pet_touch1", pose: .touch, y: -4, squash: 1.07, tilt: 3, eye: .happy, glow: 0.88, blush: 0.45, processingPhase: 0),
    FrameSpec(name: "codex_pet_touch2", pose: .touch, y: -2, squash: 1.02, tilt: -2, eye: .happy, glow: 0.78, blush: 0.35, processingPhase: 0),
    FrameSpec(name: "codex_pet_touch3", pose: .touch, y: 1, squash: 1.00, tilt: 0, eye: .codex, glow: 0.66, blush: 0.18, processingPhase: 0),
    FrameSpec(name: "codex_pet_processing0", pose: .processing, y: 0, squash: 1.00, tilt: -2, eye: .codex, glow: 0.80, blush: 0.00, processingPhase: 0),
    FrameSpec(name: "codex_pet_processing1", pose: .processing, y: -2, squash: 1.02, tilt: 1, eye: .codex, glow: 0.92, blush: 0.00, processingPhase: 1),
    FrameSpec(name: "codex_pet_processing2", pose: .processing, y: -1, squash: 1.00, tilt: 3, eye: .codex, glow: 0.86, blush: 0.00, processingPhase: 2),
    FrameSpec(name: "codex_pet_processing3", pose: .processing, y: 1, squash: 0.99, tilt: -1, eye: .codex, glow: 0.74, blush: 0.00, processingPhase: 3),
]

func hexByte(_ value: UInt8) -> String {
    String(format: "0x%02X", value)
}

func roundedRect(_ context: CGContext, rect: CGRect, radius: CGFloat) {
    context.addPath(CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil))
}

func fillRoundedRect(_ context: CGContext, rect: CGRect, radius: CGFloat, color: RGBA) {
    roundedRect(context, rect: rect, radius: radius)
    context.setFillColor(color.cgColor)
    context.fillPath()
}

func strokeLine(_ context: CGContext, from: CGPoint, to: CGPoint, color: RGBA, width: CGFloat, cap: CGLineCap = .round) {
    context.setStrokeColor(color.cgColor)
    context.setLineWidth(width)
    context.setLineCap(cap)
    context.move(to: from)
    context.addLine(to: to)
    context.strokePath()
}

func fillEllipse(_ context: CGContext, rect: CGRect, color: RGBA) {
    context.setFillColor(color.cgColor)
    context.fillEllipse(in: rect)
}

func drawLinearGradient(_ context: CGContext, rect: CGRect, radius: CGFloat, top: RGBA, bottom: RGBA) {
    context.saveGState()
    roundedRect(context, rect: rect, radius: radius)
    context.clip()
    let colors = [top.cgColor, bottom.cgColor] as CFArray
    let locations: [CGFloat] = [0.0, 1.0]
    let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: locations)!
    context.drawLinearGradient(gradient,
                               start: CGPoint(x: rect.midX, y: rect.minY),
                               end: CGPoint(x: rect.midX, y: rect.maxY),
                               options: [])
    context.restoreGState()
}

func drawGlow(_ context: CGContext, center: CGPoint, radius: CGFloat, color: RGBA, strength: CGFloat) {
    let colors = [
        RGBA(r: color.r, g: color.g, b: color.b, a: 0.28 * strength).cgColor,
        RGBA(r: color.r, g: color.g, b: color.b, a: 0.08 * strength).cgColor,
        RGBA(r: color.r, g: color.g, b: color.b, a: 0.0).cgColor,
    ] as CFArray
    let locations: [CGFloat] = [0.0, 0.45, 1.0]
    let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: locations)!
    context.drawRadialGradient(gradient,
                               startCenter: center,
                               startRadius: 2,
                               endCenter: center,
                               endRadius: radius,
                               options: [.drawsAfterEndLocation])
}

func drawFace(_ context: CGContext, rect: CGRect, eye: Eye) {
    let c = soft
    let leftX = rect.minX + 17
    let y = rect.minY + 18
    switch eye {
    case .codex:
        strokeLine(context, from: CGPoint(x: leftX + 2, y: y), to: CGPoint(x: leftX + 18, y: y + 13), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: leftX + 18, y: y + 13), to: CGPoint(x: leftX + 2, y: y + 26), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: rect.minX + 54, y: y + 28), to: CGPoint(x: rect.minX + 77, y: y + 28), color: c, width: 5)
    case .blink:
        strokeLine(context, from: CGPoint(x: leftX + 1, y: y + 15), to: CGPoint(x: leftX + 24, y: y + 15), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: rect.minX + 54, y: y + 20), to: CGPoint(x: rect.minX + 77, y: y + 20), color: c, width: 5)
    case .happy:
        strokeLine(context, from: CGPoint(x: leftX + 2, y: y + 2), to: CGPoint(x: leftX + 18, y: y + 15), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: leftX + 18, y: y + 15), to: CGPoint(x: leftX + 2, y: y + 28), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: rect.minX + 56, y: y + 27), to: CGPoint(x: rect.minX + 76, y: y + 27), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: rect.minX + 62, y: y + 34), to: CGPoint(x: rect.minX + 72, y: y + 34), color: RGBA(r: 0.48, g: 0.88, b: 1, a: 0.9), width: 3)
    case .surprised:
        strokeLine(context, from: CGPoint(x: leftX + 3, y: y), to: CGPoint(x: leftX + 17, y: y + 12), color: c, width: 5)
        strokeLine(context, from: CGPoint(x: leftX + 17, y: y + 12), to: CGPoint(x: leftX + 3, y: y + 24), color: c, width: 5)
        fillEllipse(context, rect: CGRect(x: rect.minX + 59, y: y + 16, width: 13, height: 13), color: c)
    }
}

func drawProcessingDots(_ context: CGContext, screen: CGRect, phase: Int) {
    for i in 0..<4 {
        let hot = i == phase
        let color = hot ? cyan : RGBA(r: 0.43, g: 0.54, b: 0.84, a: 0.55)
        let y = screen.maxY - 15 - CGFloat(i % 2) * 3
        fillEllipse(context,
                    rect: CGRect(x: screen.minX + 20 + CGFloat(i) * 14, y: y, width: hot ? 7 : 5, height: hot ? 7 : 5),
                    color: color)
    }
}

func drawHands(_ context: CGContext, body: CGRect, screen: CGRect, spec: FrameSpec) {
    let handSize: CGFloat = 20
    var left = CGPoint(x: screen.minX - 24, y: screen.maxY - 12)
    var right = CGPoint(x: screen.maxX + 4, y: screen.maxY - 11)

    switch spec.pose {
    case .idle:
        left.x += spec.eye == .happy ? -2 : 1
        right.x += spec.eye == .happy ? 2 : -1
        left.y += spec.eye == .happy ? -6 : 1
        right.y += spec.eye == .happy ? -6 : 1
    case .touch:
        left.x -= 7
        right.x += 7
        left.y -= 23
        right.y -= 22
    case .processing:
        let lift = CGFloat(spec.processingPhase % 2) * 7
        left.x += CGFloat((spec.processingPhase + 1) % 2) * -5
        right.x += CGFloat(spec.processingPhase % 2) * 5
        left.y -= 2 + lift
        right.y -= 2 + CGFloat((spec.processingPhase + 1) % 2) * 7
    }

    let leftRect = CGRect(x: max(body.minX - 2, left.x), y: left.y, width: handSize, height: handSize)
    let rightRect = CGRect(x: min(body.maxX - handSize + 2, right.x), y: right.y, width: handSize, height: handSize)

    fillEllipse(context, rect: leftRect, color: RGBA(r: 0.20, g: 0.66, b: 1.0, a: 0.96))
    fillEllipse(context, rect: rightRect, color: RGBA(r: 0.61, g: 0.70, b: 1.0, a: 0.96))
    fillEllipse(context,
                rect: CGRect(x: leftRect.minX + 5, y: leftRect.minY + 4, width: 6, height: 5),
                color: RGBA(r: 0.78, g: 0.95, b: 1.0, a: 0.45))
    fillEllipse(context,
                rect: CGRect(x: rightRect.minX + 5, y: rightRect.minY + 4, width: 6, height: 5),
                color: RGBA(r: 0.90, g: 0.94, b: 1.0, a: 0.42))
}

func drawPet(_ context: CGContext, spec: FrameSpec) {
    context.clear(CGRect(x: 0, y: 0, width: width, height: height))
    context.setAllowsAntialiasing(true)
    context.setShouldAntialias(true)
    context.translateBy(x: 0, y: CGFloat(height))
    context.scaleBy(x: 1, y: -1)

    let baseX: CGFloat = 22
    let baseY: CGFloat = 26 + spec.y
    let bodyW: CGFloat = 128
    let bodyH: CGFloat = 120
    let center = CGPoint(x: 86, y: 86)

    drawGlow(context, center: center, radius: 82, color: violet, strength: spec.glow)
    fillEllipse(context, rect: CGRect(x: 38, y: 136, width: 96, height: 18), color: RGBA(r: 0.10, g: 0.23, b: 0.42, a: 0.18))

    context.saveGState()
    context.translateBy(x: center.x, y: center.y + spec.y)
    context.rotate(by: spec.tilt * .pi / 180.0)
    context.scaleBy(x: 1.0 + (1.0 - spec.squash) * 0.34, y: spec.squash)
    context.translateBy(x: -center.x, y: -(center.y + spec.y))

    fillRoundedRect(context,
                    rect: CGRect(x: baseX - 8, y: baseY + 34, width: 28, height: 36),
                    radius: 11,
                    color: RGBA(r: 0.055, g: 0.12, b: 0.22, a: 0.95))
    fillRoundedRect(context,
                    rect: CGRect(x: baseX + bodyW - 20, y: baseY + 32, width: 28, height: 38),
                    radius: 11,
                    color: RGBA(r: 0.055, g: 0.12, b: 0.22, a: 0.95))

    let body = CGRect(x: baseX, y: baseY, width: bodyW, height: bodyH)
    drawLinearGradient(context, rect: body, radius: 34, top: shellTop, bottom: shellBottom)
    roundedRect(context, rect: body.insetBy(dx: 2, dy: 2), radius: 31)
    context.setStrokeColor(RGBA(r: 0.32, g: 0.73, b: 1.0, a: 0.26).cgColor)
    context.setLineWidth(2)
    context.strokePath()

    fillRoundedRect(context,
                    rect: CGRect(x: body.minX + 14, y: body.minY + 9, width: bodyW - 28, height: 30),
                    radius: 18,
                    color: RGBA(r: 0.45, g: 0.75, b: 1.0, a: 0.08))
    fillRoundedRect(context,
                    rect: CGRect(x: body.minX + 28, y: body.minY + 106, width: bodyW - 56, height: 8),
                    radius: 4,
                    color: RGBA(r: 0.01, g: 0.02, b: 0.05, a: 0.32))

    let screen = CGRect(x: body.minX + 22, y: body.minY + 28, width: bodyW - 44, height: 63)
    drawLinearGradient(context, rect: screen, radius: 20, top: screenTop, bottom: screenBottom)
    roundedRect(context, rect: screen, radius: 20)
    context.setStrokeColor(RGBA(r: cyan.r, g: cyan.g, b: cyan.b, a: 0.34 + 0.28 * spec.glow).cgColor)
    context.setLineWidth(2)
    context.strokePath()
    fillRoundedRect(context,
                    rect: CGRect(x: screen.minX + 10, y: screen.minY + 7, width: screen.width - 20, height: 9),
                    radius: 5,
                    color: RGBA(r: 0.75, g: 0.96, b: 1.0, a: 0.10))

    if spec.blush > 0 {
        fillEllipse(context, rect: CGRect(x: screen.minX + 10, y: screen.minY + 38, width: 14, height: 7), color: RGBA(r: 0.62, g: 0.38, b: 0.95, a: spec.blush))
        fillEllipse(context, rect: CGRect(x: screen.maxX - 24, y: screen.minY + 38, width: 14, height: 7), color: RGBA(r: 0.35, g: 0.82, b: 1.0, a: spec.blush))
    }

    drawFace(context, rect: screen, eye: spec.eye)
    drawHands(context, body: body, screen: screen, spec: spec)
    if spec.pose == .processing {
        drawProcessingDots(context, screen: screen, phase: spec.processingPhase)
    }
    if spec.pose == .touch {
        fillRoundedRect(context,
                        rect: CGRect(x: body.midX - 23, y: body.minY - 4, width: 46, height: 10),
                        radius: 5,
                        color: RGBA(r: 0.40, g: 0.86, b: 1.0, a: 0.18))
    }
    context.restoreGState()
}

func makeRGBA(for spec: FrameSpec) throws -> [UInt8] {
    var rgba = [UInt8](repeating: 0, count: width * height * 4)
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    try rgba.withUnsafeMutableBytes { rawBuffer in
        guard let base = rawBuffer.baseAddress else {
            throw NSError(domain: "pet-assets", code: 1)
        }
        guard let context = CGContext(data: base,
                                      width: width,
                                      height: height,
                                      bitsPerComponent: 8,
                                      bytesPerRow: width * 4,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw NSError(domain: "pet-assets", code: 2)
        }
        drawPet(context, spec: spec)
    }
    return rgba
}

func makeLVGLBytes(from rgba: [UInt8]) -> [UInt8] {
    var bytes: [UInt8] = []
    bytes.reserveCapacity(width * height * 3)
    var alpha: [UInt8] = []
    alpha.reserveCapacity(width * height)

    for y in 0..<height {
        for x in 0..<width {
            let offset = y * width * 4 + x * 4
            let a = UInt16(rgba[offset + 3])
            let r = a == 0 ? UInt16(0) : min(255, UInt16(rgba[offset]) * 255 / a)
            let g = a == 0 ? UInt16(0) : min(255, UInt16(rgba[offset + 1]) * 255 / a)
            let b = a == 0 ? UInt16(0) : min(255, UInt16(rgba[offset + 2]) * 255 / a)
            let rgb565 = UInt16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            bytes.append(UInt8(rgb565 & 0xFF))
            bytes.append(UInt8((rgb565 >> 8) & 0xFF))
            alpha.append(UInt8(a))
        }
    }
    bytes.append(contentsOf: alpha)
    return bytes
}

func writeAsset(_ spec: FrameSpec) throws {
    let rgba = try makeRGBA(for: spec)
    let bytes = makeLVGLBytes(from: rgba)
    let attr = "LV_ATTRIBUTE_IMAGE_" + spec.name.uppercased()
    var body = """
#ifdef __has_include
#if __has_include("lvgl.h")
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#define LV_LVGL_H_INCLUDE_SIMPLE
#endif
#endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef \(attr)
#define \(attr)
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST \(attr) uint8_t \(spec.name)_map[] = {
"""

    for chunkStart in stride(from: 0, to: bytes.count, by: 16) {
        let chunk = bytes[chunkStart..<min(chunkStart + 16, bytes.count)]
        body += "    " + chunk.map(hexByte).joined(separator: ", ") + ",\n"
    }

    body += """
};

const lv_image_dsc_t \(spec.name) = {
    .header.cf     = LV_COLOR_FORMAT_RGB565A8,
    .header.magic  = LV_IMAGE_HEADER_MAGIC,
    .header.w      = \(width),
    .header.h      = \(height),
    .header.stride = \(width * 2),
    .data_size     = \(bytes.count),
    .data          = \(spec.name)_map,
};
"""

    try body.write(to: outputDir.appendingPathComponent("\(spec.name).c"),
                   atomically: true,
                   encoding: .utf8)
}

func makeCGImage(from rgba: [UInt8]) throws -> CGImage {
    let provider = CGDataProvider(data: Data(rgba) as CFData)!
    guard let image = CGImage(width: width,
                              height: height,
                              bitsPerComponent: 8,
                              bitsPerPixel: 32,
                              bytesPerRow: width * 4,
                              space: CGColorSpaceCreateDeviceRGB(),
                              bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                              provider: provider,
                              decode: nil,
                              shouldInterpolate: true,
                              intent: .defaultIntent) else {
        throw NSError(domain: "pet-assets", code: 3)
    }
    return image
}

func writePreview(_ rendered: [(FrameSpec, [UInt8])]) throws {
    let cols = 4
    let scale = 1
    let cell = width * scale
    let rows = Int(ceil(Double(rendered.count) / Double(cols)))
    let previewW = cols * cell
    let previewH = rows * cell
    var rgba = [UInt8](repeating: 0, count: previewW * previewH * 4)
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    try rgba.withUnsafeMutableBytes { rawBuffer in
        guard let base = rawBuffer.baseAddress,
              let context = CGContext(data: base,
                                      width: previewW,
                                      height: previewH,
                                      bitsPerComponent: 8,
                                      bytesPerRow: previewW * 4,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw NSError(domain: "pet-assets", code: 4)
        }
        context.setFillColor(RGBA(r: 0, g: 0, b: 0, a: 1).cgColor)
        context.fill(CGRect(x: 0, y: 0, width: previewW, height: previewH))
        for (index, item) in rendered.enumerated() {
            let image = try makeCGImage(from: item.1)
            let x = (index % cols) * cell
            let y = previewH - ((index / cols) + 1) * cell
            context.draw(image, in: CGRect(x: x, y: y, width: cell, height: cell))
        }
    }

    try FileManager.default.createDirectory(at: previewURL.deletingLastPathComponent(), withIntermediateDirectories: true)
    let provider = CGDataProvider(data: Data(rgba) as CFData)!
    let image = CGImage(width: previewW,
                        height: previewH,
                        bitsPerComponent: 8,
                        bitsPerPixel: 32,
                        bytesPerRow: previewW * 4,
                        space: colorSpace,
                        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                        provider: provider,
                        decode: nil,
                        shouldInterpolate: true,
                        intent: .defaultIntent)!
    let dest = CGImageDestinationCreateWithURL(previewURL as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, image, nil)
    CGImageDestinationFinalize(dest)
}

try FileManager.default.createDirectory(at: outputDir, withIntermediateDirectories: true)
var rendered: [(FrameSpec, [UInt8])] = []
for frame in frames {
    let rgba = try makeRGBA(for: frame)
    rendered.append((frame, rgba))
    try writeAsset(frame)
}
try writePreview(rendered)
print("Generated \(frames.count) Codex pet assets")
print("Preview: \(previewURL.path)")
