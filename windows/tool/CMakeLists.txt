# 開發工具：評測正確率、查詞、模擬按鍵。對應 Sources/bangbang-tool/。
add_executable(bangbang-tool
    main.cpp
    Evaluator.h Evaluator.cpp)
target_link_libraries(bangbang-tool PRIVATE bangbang_engine bangbang_options)
# 沒有指定 --lm 時用 repo 裡和 Mac 版共用的模型檔。
target_compile_definitions(bangbang-tool PRIVATE "BANGBANG_DEFAULT_MODEL=u8\"${BANGBANG_REPO_ROOT}/build/bangbang-v3.lm\"")
