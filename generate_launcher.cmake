
file(READ "${CMAKE_CURRENT_LIST_DIR}/run-vulkan.sh.in" _content)

string(REPLACE "@AVK_TARGET_FILE@" "${AVK_TARGET_FILE}" _content "${_content}")

file(WRITE "${OUTPUT_FILE}" "${_content}")

execute_process(COMMAND chmod +x "${OUTPUT_FILE}")

