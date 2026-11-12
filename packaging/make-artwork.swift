// Draws the installers' artwork from the plugin's own logo, icon, fonts and theme colours.
//
//     swift packaging/make-artwork.swift
//
// from the repository root. Writes packaging/dmg/background.png (and @2x), and the Windows
// wizard images and icon in packaging/windows/. The results are committed, so CI does not
// need to run this; run it again after changing the logo, the colours or the DMG layout.
//
// The DMG rows here must match packaging/dmg/settings.py: each format's icon sits on the left
// at `leftX`, its folder on the right at `rightX`, one row per entry of `rows`, in order.

import AppKit
import CoreText

// MARK: - Shared

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let assets = root.appendingPathComponent("assets")
let packaging = root.appendingPathComponent("packaging")

func colour(_ hex: UInt32, _ alpha: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
            blue: CGFloat(hex & 0xff) / 255, alpha: alpha)
}

// The theme's colours (CelineUI ThemeRoles.h, source/PluginThemeRoles.h).
let ground = colour(0x17151a)     // consoleBackground: the window ground
let header = colour(0x3b334b)     // headerBackground
let accent = colour(0x9761dc)     // accent, and the wire
let graphText = colour(0xb5b3c4)  // labels
let gridDot = colour(0x5c5c5c, 0.55)

for font in ["Jura/Jura-Light.ttf", "Jura/Jura-Medium.ttf", "Jura/Jura-SemiBold.ttf"] {
    CTFontManagerRegisterFontsForURL(assets.appendingPathComponent(font) as CFURL, .process, nil)
}

func jura(_ weight: String, _ size: CGFloat) -> NSFont {
    NSFont(name: "Jura-\(weight)", size: size) ?? NSFont.systemFont(ofSize: size)
}

guard let logo = NSImage(contentsOf: assets.appendingPathComponent("icons/logo.svg")),
      let appIcon = NSImage(contentsOf: packaging.appendingPathComponent("icon.png")) else {
    fatalError("logo.svg or icon.png not found: run from the repository root")
}

/// Renders `draw` into a bitmap of `width` x `height` points at `scale` pixels per point,
/// with y running down as on screen.
func render(_ width: Int, _ height: Int, scale: CGFloat, _ draw: (NSRect) -> Void) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil,
                               pixelsWide: Int(CGFloat(width) * scale), pixelsHigh: Int(CGFloat(height) * scale),
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = NSSize(width: width, height: height)

    NSGraphicsContext.saveGraphicsState()
    let context = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = context
    context.imageInterpolation = .high

    // Flip so y runs down, as the DMG layout and every screen coordinate do.
    let transform = NSAffineTransform()
    transform.translateX(by: 0, yBy: CGFloat(height))
    transform.scaleX(by: 1, yBy: -1)
    transform.concat()

    draw(NSRect(x: 0, y: 0, width: width, height: height))
    NSGraphicsContext.restoreGraphicsState()
    return rep
}

/// Draws an image upright in the flipped context.
func drawUpright(_ image: NSImage, in rect: NSRect, alpha: CGFloat = 1) {
    NSGraphicsContext.saveGraphicsState()
    let flip = NSAffineTransform()
    flip.translateX(by: 0, yBy: rect.maxY + rect.minY)
    flip.scaleX(by: 1, yBy: -1)
    flip.concat()
    image.draw(in: rect, from: .zero, operation: .sourceOver, fraction: alpha)
    NSGraphicsContext.restoreGraphicsState()
}

func drawText(_ string: String, font: NSFont, colour: NSColor, centredAt point: NSPoint, kern: CGFloat = 0) {
    let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: colour, .kern: kern]
    let text = NSAttributedString(string: string, attributes: attributes)
    let size = text.size()
    NSGraphicsContext.saveGraphicsState()
    // Text draws upright in a flipped context only through a flipped graphics context.
    let flipped = NSGraphicsContext(cgContext: NSGraphicsContext.current!.cgContext, flipped: true)
    NSGraphicsContext.current = flipped
    text.draw(at: NSPoint(x: point.x - size.width / 2, y: point.y - size.height / 2))
    NSGraphicsContext.restoreGraphicsState()
}

/// The sheet's dot grid.
func dotGrid(_ rect: NSRect, spacing: CGFloat, radius: CGFloat) {
    gridDot.setFill()
    var y = rect.minY + spacing / 2
    while y < rect.maxY {
        var x = rect.minX + spacing / 2
        while x < rect.maxX {
            NSBezierPath(ovalIn: NSRect(x: x - radius, y: y - radius, width: 2 * radius, height: 2 * radius)).fill()
            x += spacing
        }
        y += spacing
    }
}

/// A wire as the sheet draws one: a rounded stroke with a junction dot at each end, and
/// a chevron before the far end to say which way to drag.
func wire(from a: NSPoint, to b: NSPoint, width: CGFloat) {
    accent.setStroke()
    accent.setFill()

    let line = NSBezierPath()
    line.move(to: a)
    line.line(to: b)
    line.lineWidth = width
    line.lineCapStyle = .round
    line.stroke()

    for p in [a, b] {
        let r = width * 1.9
        NSBezierPath(ovalIn: NSRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r)).fill()
    }

    let tip = NSPoint(x: b.x - width * 6, y: b.y)
    let chevron = NSBezierPath()
    chevron.move(to: NSPoint(x: tip.x - width * 2.6, y: tip.y - width * 2.6))
    chevron.line(to: tip)
    chevron.line(to: NSPoint(x: tip.x - width * 2.6, y: tip.y + width * 2.6))
    chevron.lineWidth = width
    chevron.lineCapStyle = .round
    chevron.lineJoinStyle = .round
    chevron.stroke()
}

func writePNG(_ rep: NSBitmapImageRep, _ url: URL) {
    try! FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
    print("wrote \(url.path.replacingOccurrences(of: root.path + "/", with: ""))")
}

/// A 24-bit BMP, which every Inno Setup version reads: no alpha, rows bottom up, each
/// padded to four bytes.
func writeBMP(_ rep: NSBitmapImageRep, _ url: URL) {
    let w = rep.pixelsWide, h = rep.pixelsHigh
    let rowBytes = (w * 3 + 3) & ~3
    let imageBytes = rowBytes * h
    var data = Data()

    func u16(_ v: Int) { var x = UInt16(v).littleEndian; data.append(Data(bytes: &x, count: 2)) }
    func u32(_ v: Int) { var x = UInt32(v).littleEndian; data.append(Data(bytes: &x, count: 4)) }

    data.append(contentsOf: [0x42, 0x4d]); u32(54 + imageBytes); u32(0); u32(54)
    u32(40); u32(w); u32(h); u16(1); u16(24); u32(0); u32(imageBytes); u32(2835); u32(2835); u32(0); u32(0)

    for y in stride(from: h - 1, through: 0, by: -1) {
        var row = [UInt8](repeating: 0, count: rowBytes)
        for x in 0..<w {
            let c = rep.colorAt(x: x, y: y)!.usingColorSpace(.deviceRGB)!
            row[x * 3 + 0] = UInt8(max(0, min(255, (c.blueComponent * 255).rounded())))
            row[x * 3 + 1] = UInt8(max(0, min(255, (c.greenComponent * 255).rounded())))
            row[x * 3 + 2] = UInt8(max(0, min(255, (c.redComponent * 255).rounded())))
        }
        data.append(contentsOf: row)
    }

    try! FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try! data.write(to: url)
    print("wrote \(url.path.replacingOccurrences(of: root.path + "/", with: "")) (\(w)x\(h))")
}

/// An .ico holding PNG images, one per size, as Windows has read since Vista.
func writeICO(_ sizes: [Int], _ url: URL) {
    let images: [Data] = sizes.map { size in
        let rep = render(size, size, scale: 1) { rect in
            drawUpright(appIcon, in: rect)
        }
        return rep.representation(using: .png, properties: [:])!
    }

    var data = Data()
    func u8(_ v: Int) { data.append(UInt8(v & 0xff)) }
    func u16(_ v: Int) { var x = UInt16(v).littleEndian; data.append(Data(bytes: &x, count: 2)) }
    func u32(_ v: Int) { var x = UInt32(v).littleEndian; data.append(Data(bytes: &x, count: 4)) }

    u16(0); u16(1); u16(sizes.count)
    var offset = 6 + 16 * sizes.count

    for (size, png) in zip(sizes, images) {
        u8(size >= 256 ? 0 : size); u8(size >= 256 ? 0 : size); u8(0); u8(0)
        u16(1); u16(32); u32(png.count); u32(offset)
        offset += png.count
    }

    for png in images { data.append(png) }
    try! data.write(to: url)
    print("wrote \(url.path.replacingOccurrences(of: root.path + "/", with: "")) (\(sizes.map(String.init).joined(separator: ", ")))")
}

// MARK: - The DMG window

// Window and layout, in points. Kept in step with packaging/dmg/settings.py by hand.
// Compact enough that the five rows (down to 572) show with every Finder bar on -- tab,
// path and status bars, 124 points of a 712-point window between them -- and the picture
// runs on as plain grid to 700, past what the window shows with none (see settings.py).
let dmgWidth = 760, dmgHeight = 700
let headerHeight: CGFloat = 92
let leftX: CGFloat = 230, rightX: CGFloat = 530
let firstRowY: CGFloat = 144, rowPitch: CGFloat = 94
let rows = ["APP", "AU", "VST3", "CLAP", "LV2"]

func dmgBackground(scale: CGFloat) -> NSBitmapImageRep {
    render(dmgWidth, dmgHeight, scale: scale) { rect in
        ground.setFill()
        rect.fill()
        dotGrid(rect, spacing: 16, radius: 0.9)

        // The header band, holding the logo alone: the rows say what to do.
        header.setFill()
        NSRect(x: 0, y: 0, width: rect.width, height: headerHeight).fill()

        let logoHeight: CGFloat = 40
        let logoWidth = logoHeight * logo.size.width / logo.size.height
        drawUpright(logo, in: NSRect(x: (rect.width - logoWidth) / 2, y: (headerHeight - logoHeight) / 2,
                                     width: logoWidth, height: logoHeight))

        // One row per format: a tile under each icon and its name, so Finder's label
        // reads whether it draws it white (dark mode) or black (light mode). The tile's
        // tone sits between the two: about 5:1 against white and 4:1 against black.
        let tile = colour(0x726a87)

        for (i, format) in rows.enumerated() {
            let y = firstRowY + CGFloat(i) * rowPitch

            for x in [leftX, rightX] {
                tile.setFill()
                // Finder centres the 56-point icon on its position and sets its name about
                // 3 points under it, 16 high: the tile covers both with room to spare.
                NSBezierPath(roundedRect: NSRect(x: x - 68, y: y - 36, width: 136, height: 88),
                             xRadius: 14, yRadius: 14).fill()
            }

            wire(from: NSPoint(x: leftX + 88, y: y - 4), to: NSPoint(x: rightX - 88, y: y - 4), width: 3)
            drawText(format, font: jura("SemiBold", 12), colour: graphText,
                     centredAt: NSPoint(x: (leftX + rightX) / 2, y: y - 22), kern: 1.5)
        }

    }
}

writePNG(dmgBackground(scale: 1), packaging.appendingPathComponent("dmg/background.png"))
writePNG(dmgBackground(scale: 2), packaging.appendingPathComponent("dmg/background@2x.png"))

// MARK: - The Windows wizard

// The tall image down the left of the welcome and finish pages, at 100%, 150% and 200%
// (Inno Setup picks the closest to the screen's scaling). Same ground, grid and wire as
// the DMG.
func wizardImage(scale: CGFloat) -> NSBitmapImageRep {
    render(164, 314, scale: scale) { rect in
        ground.setFill()
        rect.fill()
        dotGrid(rect, spacing: 12, radius: 0.7)

        let icon: CGFloat = 92
        let iconRect = NSRect(x: (rect.width - icon) / 2, y: 70, width: icon, height: icon)
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(roundedRect: iconRect, xRadius: 20, yRadius: 20).addClip()
        drawUpright(appIcon, in: iconRect)
        NSGraphicsContext.restoreGraphicsState()

        wire(from: NSPoint(x: 24, y: 214), to: NSPoint(x: rect.width - 24, y: 214), width: 2.2)

        let logoWidth: CGFloat = 104
        let logoHeight = logoWidth * logo.size.height / logo.size.width
        drawUpright(logo, in: NSRect(x: (rect.width - logoWidth) / 2, y: 240, width: logoWidth, height: logoHeight))
    }
}

// The small image at the top right of the inner pages, on the wizard's white header.
func wizardSmallImage(scale: CGFloat) -> NSBitmapImageRep {
    render(55, 55, scale: scale) { rect in
        NSColor.white.setFill()
        rect.fill()
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(roundedRect: rect.insetBy(dx: 3, dy: 3), xRadius: 9, yRadius: 9).addClip()
        drawUpright(appIcon, in: rect.insetBy(dx: 3, dy: 3))
        NSGraphicsContext.restoreGraphicsState()
    }
}

let windows = packaging.appendingPathComponent("windows")

for (scale, suffix) in [(CGFloat(1), "100"), (1.5, "150"), (2, "200")] {
    writeBMP(wizardImage(scale: scale), windows.appendingPathComponent("wizard-\(suffix).bmp"))
    writeBMP(wizardSmallImage(scale: scale), windows.appendingPathComponent("wizard-small-\(suffix).bmp"))
}

writeICO([16, 24, 32, 48, 64, 128, 256], windows.appendingPathComponent("icon.ico"))

// MARK: - The DMG's volume icon

// An .icns from the app icon, through an .iconset and Apple's iconutil.
do {
    let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("Celine.iconset")
    try? FileManager.default.removeItem(at: iconset)
    try! FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)

    for size in [16, 32, 128, 256, 512] {
        for scale in [1, 2] {
            let pixels = size * scale
            let rep = render(pixels, pixels, scale: 1) { rect in
                NSBezierPath(roundedRect: rect.insetBy(dx: CGFloat(pixels) * 0.09, dy: CGFloat(pixels) * 0.09),
                             xRadius: CGFloat(pixels) * 0.18, yRadius: CGFloat(pixels) * 0.18).addClip()
                drawUpright(appIcon, in: rect.insetBy(dx: CGFloat(pixels) * 0.09, dy: CGFloat(pixels) * 0.09))
            }
            let name = scale == 1 ? "icon_\(size)x\(size).png" : "icon_\(size)x\(size)@2x.png"
            try! rep.representation(using: .png, properties: [:])!.write(to: iconset.appendingPathComponent(name))
        }
    }

    let output = packaging.appendingPathComponent("dmg/volume.icns")
    let iconutil = Process()
    iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
    iconutil.arguments = ["-c", "icns", iconset.path, "-o", output.path]
    try! iconutil.run()
    iconutil.waitUntilExit()
    precondition(iconutil.terminationStatus == 0, "iconutil failed")
    try? FileManager.default.removeItem(at: iconset)
    print("wrote packaging/dmg/volume.icns")
}
