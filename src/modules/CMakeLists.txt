# One directory per GUI module. Each appends itself to SKG_MODULE_TARGETS and
# SKG_TRANSLATED_TARGETS; app/ links the former, the root collects the latter.
add_subdirectory(placeholder)
add_subdirectory(settings)
add_subdirectory(tdvms)
add_subdirectory(features)
add_subdirectory(analysis)
add_subdirectory(classification)
add_subdirectory(earlywarning)
add_subdirectory(indicators)
add_subdirectory(study)

set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
