# 測試用小程式：手動測試視窗與 --selftest 自動測試。x86 版用來測 32 位元程式。
# 候選窗的繪製（CandidateRenderer）和輸入法 DLL 共用同一份原始碼，給 --render-candidates 用。
add_executable(testpad main.cpp ../tip/CandidateRenderer.cpp)
target_include_directories(testpad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tip")
target_link_libraries(testpad PRIVATE bangbang_engine bangbang_options ole32 oleaut32 uuid user32 gdi32 advapi32 d2d1 dwrite windowscodecs)
target_compile_definitions(testpad PRIVATE "BANGBANG_DEFAULT_MODEL=u8\"${BANGBANG_REPO_ROOT}/build/bangbang-v3.lm\"")
