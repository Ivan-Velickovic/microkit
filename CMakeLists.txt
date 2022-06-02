# apparently, order is quite important, do not reorder statements!
cmake_minimum_required(VERSION 3.7.2)

include(CMakePrintHelpers)

list(
    APPEND
        CMAKE_MODULE_PATH
        "${CMAKE_CURRENT_LIST_DIR}/kernel"
        "${CMAKE_CURRENT_LIST_DIR}/capdl"
        "${CMAKE_CURRENT_LIST_DIR}/seL4_tools/cmake-tool/helpers"
        "${CMAKE_CURRENT_LIST_DIR}/musllibc"
        "${CMAKE_CURRENT_LIST_DIR}/sel4runtime"
        "${CMAKE_CURRENT_LIST_DIR}/seL4_libs"
        "${CMAKE_CURRENT_LIST_DIR}/util_libs"
        "${CMAKE_CURRENT_LIST_DIR}/seL4_tools/elfloader-tool"
)

include(${CMAKE_CURRENT_LIST_DIR}/settings.cmake)
if(NOT DEFINED SEL4CP_BUILD_DIR)
    message(FATAL_ERROR "SEL4CP_BUILD_DIR not defined.")
elseif(NOT DEFINED SEL4CP_CDL_FILENAME)
    message(FATAL_ERROR "SEL4CP_CDL_FILENAME not defined.")
elseif(NOT DEFINED SEL4CP_ELF_FILENAMES)
    message(FATAL_ERROR "SEL4CP_ELF_FILENAMES not defined.")
endif()

# FIXME: use non mcs kernel for case initializer
set(KernelIsMCS ON)
set(CROSS_COMPILER_PREFIX "aarch64-none-elf-")
set(CapDLLoaderMaxObjects 20000)
set(KernelRootCNodeSizeBits 17)

include(application_settings)
correct_platform_strings()
find_package(seL4 REQUIRED)
sel4_configure_platform_settings()
ApplyCommonReleaseVerificationSettings(FALSE FALSE)

sel4_import_kernel()
if(KernelArchARM)
    ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})
endif()
find_package(elfloader-tool REQUIRED)
elfloader_import_project()

project(hello-capdl C ASM)

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)
find_package(capdl REQUIRED)

sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()
musllibc_setup_build_environment_with_sel4runtime()

CapDLToolInstall(install_capdl_tool CAPDL_TOOL_BINARY)
capdl_import_project()
set(CapDLStaticAlloc OFF)
set(CDL_TARGET "${CMAKE_CURRENT_BINARY_DIR}/${SEL4CP_CDL_FILENAME}")
set(SEL4CP_CDL_FILE "${SEL4CP_BUILD_DIR}/${SEL4CP_CDL_FILENAME}")
# from https://stackoverflow.com/a/55184207
add_custom_target(copy_cdl DEPENDS "${CDL_TARGET}")
add_custom_command(OUTPUT "${CDL_TARGET}"
                   COMMAND ${CMAKE_COMMAND} -E copy "${SEL4CP_CDL_FILE}" "${CDL_TARGET}"
                   DEPENDS "${SEL4CP_CDL_FILE}")
CapDLToolCFileGen(capdl_c_spec_target capdl_spec.c ${CapDLStaticAlloc} "$<TARGET_PROPERTY:object_sizes,FILE_PATH>" "${CDL_TARGET}" "${CAPDL_TOOL_BINARY}"
    DEPENDS copy_cdl install_capdl_tool "${CAPDL_TOOL_BINARY}")

set(SEL4CP_ELFS "")
set(CDL_ELFS "")
foreach(elf IN LISTS SEL4CP_ELF_FILENAMES)
    list(APPEND SEL4CP_ELFS "${SEL4CP_BUILD_DIR}/${elf}") 
    list(APPEND CDL_ELFS "${CMAKE_CURRENT_BINARY_DIR}/${elf}") 
endforeach()
add_custom_target(copy_elfs DEPENDS ${CDL_ELFS})
add_custom_command(OUTPUT ${CDL_ELFS}
                   COMMAND ${CMAKE_COMMAND} -E copy ${SEL4CP_ELFS} "${CMAKE_CURRENT_BINARY_DIR}"
                   DEPENDS ${SEL4CP_ELFS})
BuildCapDLApplication(C_SPEC capdl_spec.c
ELF ${CDL_ELFS}
DEPENDS capdl_c_spec_target
OUTPUT capdl-loader)

include(rootserver)
DeclareRootserver(capdl-loader)

if("${PLATFORM}" STREQUAL "qemu-arm-virt")
    include(simulation)
    GenerateSimulateScript()
endif()
