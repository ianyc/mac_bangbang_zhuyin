import AppKit
import InputMethodKit

// 輸入法程式進入點：向系統註冊 IMKServer，之後由系統把按鍵送給 BangbangInputController。
let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String ?? "BangbangZhuyin_1_Connection"
guard let bundleID = Bundle.main.bundleIdentifier,
      let server = IMKServer(name: connectionName, bundleIdentifier: bundleID) else {
    NSLog("BangbangZhuyin: 無法建立 IMKServer")
    exit(1)
}
// 啟動時先載入語言模型，第一次打字就不必等待。
_ = Shared.lm
_ = Shared.userModel
withExtendedLifetime(server) {
    NSApplication.shared.run()
}
