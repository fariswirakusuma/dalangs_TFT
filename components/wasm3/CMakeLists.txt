set(WASM3_SRC "${CMAKE_CURRENT_LIST_DIR}/../../third_party/wasm3/source")

idf_component_register(
    SRCS
        "${WASM3_SRC}/m3_bind.c"
        "${WASM3_SRC}/m3_code.c"
        "${WASM3_SRC}/m3_compile.c"
        "${WASM3_SRC}/m3_core.c"
        "${WASM3_SRC}/m3_emit.c"
        "${WASM3_SRC}/m3_env.c"
        "${WASM3_SRC}/m3_exec.c"
        "${WASM3_SRC}/m3_function.c"
        "${WASM3_SRC}/m3_info.c"
        "${WASM3_SRC}/m3_module.c"
        "${WASM3_SRC}/m3_parse.c"
    INCLUDE_DIRS
        "${WASM3_SRC}"
)

# Keep the interpreter in flash: IRAM is needed by the camera/SPI paths.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ESP32
    M3_IN_IRAM
    d_m3Use32BitSlots=1
    d_m3MaxFunctionStackHeight=256
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-strict-aliasing
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-int-to-pointer-cast
)
