// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "BunbunZhuyin",
    platforms: [.macOS(.v13)],
    targets: [
        // 選字引擎：注音、詞庫、語言模型、組字狀態機。不依賴 AppKit，方便測試。
        .target(name: "BunbunEngine"),
        // 輸入法本體（InputMethodKit 外殼）。
        .executableTarget(
            name: "BunbunZhuyin",
            dependencies: ["BunbunEngine"],
            linkerSettings: [.linkedFramework("InputMethodKit")]
        ),
        // 開發工具：建語言模型、評測正確率、轉換測試、註冊輸入法。
        .executableTarget(
            name: "bunbun-tool",
            dependencies: ["BunbunEngine"],
            linkerSettings: [.linkedFramework("Carbon")]
        ),
        .testTarget(name: "BunbunEngineTests", dependencies: ["BunbunEngine"]),
    ]
)
