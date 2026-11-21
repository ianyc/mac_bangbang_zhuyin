# 測試用小程式：手動測試視窗與 --selftest 自動測試。x86 版用來測 32 位元程式。
add_executable(testpad main.cpp)
target_include_directories(testpad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tip")
target_link_libraries(testpad PRIVATE bangbang_engine bangbang_options ole32 oleaut32 uuid user32 gdi32)
target_compile_definitions(testpad PRIVATE "BANGBANG_DEFAULT_MODEL=u8\"${BANGBANG_REPO_ROOT}/build/bangbang-v3.lm\"")
