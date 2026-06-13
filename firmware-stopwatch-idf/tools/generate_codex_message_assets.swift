import CoreGraphics
import CoreText
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

struct MessageAsset {
    let name: String
    let text: String
    let tint: RGBA
}

let width = 260
let height = 34
let outputDir = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    .appendingPathComponent("main/assets/images")
let previewURL = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
    .appendingPathComponent("build/codex_pet_message_preview.png")

let blue = RGBA(r: 0.27, g: 0.85, b: 1.0, a: 1.0)
let violet = RGBA(r: 0.56, g: 0.70, b: 1.0, a: 1.0)
let soft = RGBA(r: 0.84, g: 0.96, b: 1.0, a: 1.0)

let assets: [MessageAsset] = [
    MessageAsset(name: "codex_pet_msg_ready", text: "我在待命", tint: soft),
    MessageAsset(name: "codex_pet_msg_touch", text: "收到摸摸", tint: blue),
    MessageAsset(name: "codex_pet_msg_shake", text: "我抓稳了", tint: violet),
    MessageAsset(name: "codex_pet_msg_processing", text: "我在想", tint: blue),
    MessageAsset(name: "codex_pet_msg_idle_0", text: "我在待命", tint: soft),
    MessageAsset(name: "codex_pet_msg_idle_1", text: "随时叫我", tint: blue),
    MessageAsset(name: "codex_pet_msg_idle_2", text: "今天也在线", tint: violet),
    MessageAsset(name: "codex_pet_msg_touch_0", text: "别戳啦", tint: blue),
    MessageAsset(name: "codex_pet_msg_touch_1", text: "收到摸摸", tint: soft),
    MessageAsset(name: "codex_pet_msg_touch_2", text: "我醒啦", tint: violet),
    MessageAsset(name: "codex_pet_msg_touch_3", text: "再戳会害羞", tint: blue),
    MessageAsset(name: "codex_pet_msg_shake_0", text: "晕啦", tint: violet),
    MessageAsset(name: "codex_pet_msg_shake_1", text: "慢点摇", tint: soft),
    MessageAsset(name: "codex_pet_msg_shake_2", text: "重力变化中", tint: blue),
    MessageAsset(name: "codex_pet_msg_shake_3", text: "我抓稳了", tint: violet),
    MessageAsset(name: "codex_pet_msg_processing_0", text: "我在想", tint: blue),
    MessageAsset(name: "codex_pet_msg_processing_1", text: "马上好", tint: soft),
    MessageAsset(name: "codex_pet_msg_processing_2", text: "读上下文中", tint: violet),
    MessageAsset(name: "codex_pet_msg_processing_3", text: "别催别催", tint: blue),
    MessageAsset(name: "codex_pet_msg_key_0", text: "发出去了", tint: soft),
    MessageAsset(name: "codex_pet_msg_key_1", text: "收到指令", tint: blue),
    MessageAsset(name: "codex_pet_msg_key_2", text: "我按好了", tint: violet),
    MessageAsset(name: "codex_pet_msg_error_0", text: "没打中", tint: violet),
    MessageAsset(name: "codex_pet_msg_error_1", text: "再试一次", tint: soft),
    MessageAsset(name: "codex_pet_msg_error_2", text: "主机有点忙", tint: blue),
]

func hexByte(_ value: UInt8) -> String {
    String(format: "0x%02X", value)
}

func drawRoundedRect(_ context: CGContext, rect: CGRect, radius: CGFloat, fill: RGBA, stroke: RGBA) {
    let path = CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)
    context.addPath(path)
    context.setFillColor(fill.cgColor)
    context.fillPath()
    context.addPath(path)
    context.setStrokeColor(stroke.cgColor)
    context.setLineWidth(1)
    context.strokePath()
}

func drawText(_ context: CGContext, _ text: String, tint: RGBA) {
    let font = CTFontCreateWithName("PingFangSC-Semibold" as CFString, 19, nil)
    let attrs: [CFString: Any] = [
        kCTFontAttributeName: font,
        kCTForegroundColorAttributeName: tint.cgColor,
    ]
    let attributed = CFAttributedStringCreate(nil, text as CFString, attrs as CFDictionary)!
    let line = CTLineCreateWithAttributedString(attributed)
    var ascent: CGFloat = 0
    var descent: CGFloat = 0
    var leading: CGFloat = 0
    let lineWidth = CGFloat(CTLineGetTypographicBounds(line, &ascent, &descent, &leading))
    context.setShadow(offset: .zero, blur: 6, color: RGBA(r: tint.r, g: tint.g, b: tint.b, a: 0.45).cgColor)
    context.textPosition = CGPoint(x: (CGFloat(width) - lineWidth) / 2,
                                   y: (CGFloat(height) - ascent - descent) / 2 + descent - 1)
    CTLineDraw(line, context)
    context.setShadow(offset: .zero, blur: 0, color: nil)
}

func makeRGBA(for asset: MessageAsset) throws -> [UInt8] {
    var rgba = [UInt8](repeating: 0, count: width * height * 4)
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    try rgba.withUnsafeMutableBytes { rawBuffer in
        guard let base = rawBuffer.baseAddress else {
            throw NSError(domain: "message-assets", code: 1)
        }
        guard let context = CGContext(data: base,
                                      width: width,
                                      height: height,
                                      bitsPerComponent: 8,
                                      bytesPerRow: width * 4,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw NSError(domain: "message-assets", code: 2)
        }
        context.clear(CGRect(x: 0, y: 0, width: width, height: height))
        context.setAllowsAntialiasing(true)
        context.setShouldAntialias(true)
        drawRoundedRect(context,
                        rect: CGRect(x: 4, y: 2, width: width - 8, height: height - 4),
                        radius: 15,
                        fill: RGBA(r: 0.03, g: 0.11, b: 0.18, a: 0.72),
                        stroke: RGBA(r: 0.24, g: 0.62, b: 1.0, a: 0.20))
        drawText(context, asset.text, tint: asset.tint)
    }

    return rgba
}

func makePixels(from rgba: [UInt8]) -> [UInt8] {
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

func writeAsset(_ asset: MessageAsset, rgba: [UInt8]) throws {
    let bytes = makePixels(from: rgba)
    let attr = "LV_ATTRIBUTE_IMAGE_" + asset.name.uppercased()
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

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST \(attr) uint8_t \(asset.name)_map[] = {
"""

    for chunkStart in stride(from: 0, to: bytes.count, by: 16) {
        let chunk = bytes[chunkStart..<min(chunkStart + 16, bytes.count)]
        body += "    " + chunk.map(hexByte).joined(separator: ", ") + ",\n"
    }

    body += """
};

const lv_image_dsc_t \(asset.name) = {
    .header.cf     = LV_COLOR_FORMAT_RGB565A8,
    .header.magic  = LV_IMAGE_HEADER_MAGIC,
    .header.w      = \(width),
    .header.h      = \(height),
    .header.stride = \(width * 2),
    .data_size     = \(bytes.count),
    .data          = \(asset.name)_map,
};
"""

    try body.write(to: outputDir.appendingPathComponent("\(asset.name).c"),
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
        throw NSError(domain: "message-assets", code: 3)
    }
    return image
}

func writePreview(_ rendered: [(MessageAsset, [UInt8])]) throws {
    let cols = 3
    let gap = 8
    let cellW = width
    let cellH = height
    let rows = Int(ceil(Double(rendered.count) / Double(cols)))
    let previewW = cols * cellW + (cols + 1) * gap
    let previewH = rows * cellH + (rows + 1) * gap
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
            throw NSError(domain: "message-assets", code: 4)
        }
        context.setFillColor(RGBA(r: 0, g: 0, b: 0, a: 1).cgColor)
        context.fill(CGRect(x: 0, y: 0, width: previewW, height: previewH))
        for (index, item) in rendered.enumerated() {
            let image = try makeCGImage(from: item.1)
            let x = gap + (index % cols) * (cellW + gap)
            let y = previewH - gap - ((index / cols) + 1) * cellH - (index / cols) * gap
            context.draw(image, in: CGRect(x: x, y: y, width: cellW, height: cellH))
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
var rendered: [(MessageAsset, [UInt8])] = []
for asset in assets {
    let rgba = try makeRGBA(for: asset)
    rendered.append((asset, rgba))
    try writeAsset(asset, rgba: rgba)
}
try writePreview(rendered)
print("Generated \(assets.count) Codex message assets")
print("Preview: \(previewURL.path)")
