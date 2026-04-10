import Accelerate
import Cocoa
import CoreVideo
import Darwin
import FlutterMacOS
import IOSurface

// MARK: - Legacy RGBA upload texture (backward compatibility)

private final class EngineHostTexture: NSObject, FlutterTexture {
  private let lock = NSLock()
  private var pixelBuffer: CVPixelBuffer?
  private var pixelWidth: Int = 0
  private var pixelHeight: Int = 0

  func copyPixelBuffer() -> Unmanaged<CVPixelBuffer>? {
    lock.lock()
    defer { lock.unlock() }
    guard let pixelBuffer else {
      return nil
    }
    return Unmanaged.passRetained(pixelBuffer)
  }

  func updateFrame(
    rgbaData: Data,
    width: Int,
    height: Int,
    rowBytes: Int
  ) -> Bool {
    guard width > 0, height > 0, rowBytes >= width * 4 else {
      return false
    }

    lock.lock()
    if pixelBuffer == nil || pixelWidth != width || pixelHeight != height {
      var buffer: CVPixelBuffer?
      let attrs: [CFString: Any] = [
        kCVPixelBufferCGImageCompatibilityKey: true,
        kCVPixelBufferCGBitmapContextCompatibilityKey: true,
        kCVPixelBufferIOSurfacePropertiesKey: [:],
      ]
      let status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        width,
        height,
        kCVPixelFormatType_32BGRA,
        attrs as CFDictionary,
        &buffer
      )
      guard status == kCVReturnSuccess, let newBuffer = buffer else {
        lock.unlock()
        return false
      }
      pixelBuffer = newBuffer
      pixelWidth = width
      pixelHeight = height
    }
    guard let pixelBuffer else {
      lock.unlock()
      return false
    }
    lock.unlock()

    CVPixelBufferLockBaseAddress(pixelBuffer, [])
    defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, []) }
    guard let baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer) else {
      return false
    }
    let targetStride = CVPixelBufferGetBytesPerRow(pixelBuffer)
    return rgbaData.withUnsafeBytes { bytes in
      guard let srcBase = bytes.baseAddress else {
        return false
      }
      var srcBuffer = vImage_Buffer(
        data: UnsafeMutableRawPointer(mutating: srcBase),
        height: vImagePixelCount(height),
        width: vImagePixelCount(width),
        rowBytes: rowBytes
      )
      var dstBuffer = vImage_Buffer(
        data: baseAddress,
        height: vImagePixelCount(height),
        width: vImagePixelCount(width),
        rowBytes: targetStride
      )
      var map: [UInt8] = [2, 1, 0, 3]  // RGBA -> BGRA
      let convertResult = vImagePermuteChannels_ARGB8888(
        &srcBuffer,
        &dstBuffer,
        &map,
        vImage_Flags(kvImageNoFlags)
      )
      return convertResult == kvImageNoError
    }
  }
}

// MARK: - IOSurface zero-copy texture

private final class EngineIOSurfaceTexture: NSObject, FlutterTexture {
  private let lock = NSLock()
  private var pixelBuffer: CVPixelBuffer?
  private(set) var ioSurfaceID: UInt32 = 0
  private(set) var pixelWidth: Int = 0
  private(set) var pixelHeight: Int = 0

  /// Create (or recreate) the IOSurface-backed CVPixelBuffer.
  /// Returns the IOSurfaceID that the C++ engine should render into.
  func createSurface(width: Int, height: Int) -> UInt32? {
    guard width > 0, height > 0 else { return nil }

    lock.lock()
    defer { lock.unlock() }

    // Reuse if size unchanged
    if pixelWidth == width && pixelHeight == height && ioSurfaceID != 0 {
      return ioSurfaceID
    }

    // Create IOSurface-backed CVPixelBuffer (BGRA format for Metal/Flutter)
    var buffer: CVPixelBuffer?
    let attrs: [CFString: Any] = [
      kCVPixelBufferCGImageCompatibilityKey: true,
      kCVPixelBufferCGBitmapContextCompatibilityKey: true,
      kCVPixelBufferIOSurfacePropertiesKey: [:] as [String: Any],
      kCVPixelBufferMetalCompatibilityKey: true,
    ]
    let status = CVPixelBufferCreate(
      kCFAllocatorDefault,
      width,
      height,
      kCVPixelFormatType_32BGRA,
      attrs as CFDictionary,
      &buffer
    )
    guard status == kCVReturnSuccess, let newBuffer = buffer else {
      return nil
    }

    // Get the IOSurface from the CVPixelBuffer
    guard let surfaceUnmanaged = CVPixelBufferGetIOSurface(newBuffer) else {
      return nil
    }
    let surface = surfaceUnmanaged.takeUnretainedValue()
    let surfaceID = IOSurfaceGetID(surface)

    pixelBuffer = newBuffer
    pixelWidth = width
    pixelHeight = height
    ioSurfaceID = surfaceID

    return surfaceID
  }

  func copyPixelBuffer() -> Unmanaged<CVPixelBuffer>? {
    lock.lock()
    defer { lock.unlock() }
    guard let pixelBuffer else { return nil }
    return Unmanaged.passRetained(pixelBuffer)
  }
}

// MARK: - Plugin

public class FlutterEngineBridgePlugin: NSObject, FlutterPlugin {
  private let registrar: FlutterPluginRegistrar
  private var textures: [Int64: EngineHostTexture] = [:]
  private var ioSurfaceTextures: [Int64: EngineIOSurfaceTexture] = [:]

  init(registrar: FlutterPluginRegistrar) {
    self.registrar = registrar
    super.init()
  }

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(
      name: "flutter_engine_bridge",
      binaryMessenger: registrar.messenger
    )
    let instance = FlutterEngineBridgePlugin(registrar: registrar)
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  deinit {
    for (textureId, _) in textures {
      registrar.textures.unregisterTexture(textureId)
    }
    for (textureId, _) in ioSurfaceTextures {
      registrar.textures.unregisterTexture(textureId)
    }
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "getPlatformVersion":
      result("macOS " + ProcessInfo.processInfo.operatingSystemVersionString)

    case "forceQuitApp":
      result(nil)
      DispatchQueue.main.async {
        fflush(nil)
        Darwin._exit(0)
      }

    // --- Legacy RGBA texture ---
    case "createTexture":
      let texture = EngineHostTexture()
      let textureId = registrar.textures.register(texture)
      textures[textureId] = texture
      result(textureId)

    case "updateTextureRgba":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber,
        let rgba = args["rgba"] as? FlutterStandardTypedData,
        let widthNumber = args["width"] as? NSNumber,
        let heightNumber = args["height"] as? NSNumber,
        let rowBytesNumber = args["rowBytes"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message:
              "updateTextureRgba requires textureId/rgba/width/height/rowBytes",
            details: nil
          )
        )
        return
      }

      let textureId = textureIdNumber.int64Value
      let width = widthNumber.intValue
      let height = heightNumber.intValue
      let rowBytes = rowBytesNumber.intValue
      guard let texture = textures[textureId] else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "Texture id not found",
            details: nil
          )
        )
        return
      }

      guard texture.updateFrame(
        rgbaData: rgba.data,
        width: width,
        height: height,
        rowBytes: rowBytes
      ) else {
        result(
          FlutterError(
            code: "texture_update_failed",
            message: "Failed to update texture frame",
            details: nil
          )
        )
        return
      }

      registrar.textures.textureFrameAvailable(textureId)
      result(nil)

    case "disposeTexture":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "disposeTexture requires textureId",
            details: nil
          )
        )
        return
      }
      let textureId = textureIdNumber.int64Value
      textures.removeValue(forKey: textureId)
      registrar.textures.unregisterTexture(textureId)
      result(nil)

    // --- IOSurface zero-copy texture ---
    case "createIOSurfaceTexture":
      guard let args = call.arguments as? [String: Any],
        let widthNumber = args["width"] as? NSNumber,
        let heightNumber = args["height"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "createIOSurfaceTexture requires width/height",
            details: nil
          )
        )
        return
      }

      let width = widthNumber.intValue
      let height = heightNumber.intValue
      let texture = EngineIOSurfaceTexture()
      guard let ioSurfaceID = texture.createSurface(width: width, height: height) else {
        result(
          FlutterError(
            code: "iosurface_failed",
            message: "Failed to create IOSurface-backed texture",
            details: nil
          )
        )
        return
      }

      let textureId = registrar.textures.register(texture)
      ioSurfaceTextures[textureId] = texture
      result([
        "textureId": textureId,
        "ioSurfaceID": Int(ioSurfaceID),
        "width": width,
        "height": height,
      ] as [String: Any])

    case "notifyFrameAvailable":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "notifyFrameAvailable requires textureId",
            details: nil
          )
        )
        return
      }
      let textureId = textureIdNumber.int64Value
      registrar.textures.textureFrameAvailable(textureId)
      result(nil)

    case "resizeIOSurfaceTexture":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber,
        let widthNumber = args["width"] as? NSNumber,
        let heightNumber = args["height"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "resizeIOSurfaceTexture requires textureId/width/height",
            details: nil
          )
        )
        return
      }
      let textureId = textureIdNumber.int64Value
      let width = widthNumber.intValue
      let height = heightNumber.intValue

      guard let texture = ioSurfaceTextures[textureId] else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "IOSurface texture id not found",
            details: nil
          )
        )
        return
      }

      guard let newIOSurfaceID = texture.createSurface(width: width, height: height) else {
        result(
          FlutterError(
            code: "iosurface_failed",
            message: "Failed to resize IOSurface",
            details: nil
          )
        )
        return
      }

      result([
        "textureId": textureId,
        "ioSurfaceID": Int(newIOSurfaceID),
        "width": width,
        "height": height,
      ] as [String: Any])

    case "disposeIOSurfaceTexture":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "disposeIOSurfaceTexture requires textureId",
            details: nil
          )
        )
        return
      }
      let textureId = textureIdNumber.int64Value
      ioSurfaceTextures.removeValue(forKey: textureId)
      registrar.textures.unregisterTexture(textureId)
      result(nil)

    default:
      result(FlutterMethodNotImplemented)
    }
  }
}
