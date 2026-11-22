# 測試用小程式：手動測試視窗與 --selftest 自動測試。x86 版用來測 32 位元程式。
# 候選窗（CandidateRenderer、CandidateWindow）和輸入法 DLL 共用同一份原始碼，給 --render-candidates 與 --time-candidates 用。
add_executable(testpad main.cpp ../tip/CandidateRenderer.cpp ../tip/CandidateWindow.cpp)
target_include_directories(testpad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tip")
target_link_libraries(testpad PRIVATE bangbang_engine bangbang_options ole32 oleaut32 uuid user32 gdi32 advapi32
    d2d1 dwrite dwmapi shcore windowscodecs delayimp)
# 和 DLL 一樣延遲載入繪圖元件：--time-candidates 量到的時間才包含載入它們。
target_link_options(testpad PRIVATE /DELAYLOAD:d2d1.dll /DELAYLOAD:dwrite.dll /DELAYLOAD:dwmapi.dll)
target_compile_definitions(testpad PRIVATE "BANGBANG_DEFAULT_MODEL=u8\"${BANGBANG_REPO_ROOT}/build/bangbang-v3.lm\"")
