# Runs spirv-val on SPV (cmake -DSPIRV_VAL=<exe> -DSPV=<file> -P). Deletes an invalid file, so a failed
# validation can't leave a .spv that looks up to date.
execute_process(
    COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 --scalar-block-layout "${SPV}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    file(REMOVE "${SPV}")
    message(FATAL_ERROR "spirv-val rejected ${SPV}")
endif()
