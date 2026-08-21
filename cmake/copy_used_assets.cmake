# Copies only runtime assets referenced by assets/visuals.json.
# Expected -D vars:
#   SOURCE_ASSETS_DIR  - repo assets/ directory
#   DEST_ASSETS_DIR    - output assets/ next to the executable

if(NOT DEFINED SOURCE_ASSETS_DIR OR NOT DEFINED DEST_ASSETS_DIR)
    message(FATAL_ERROR "copy_used_assets.cmake requires SOURCE_ASSETS_DIR and DEST_ASSETS_DIR")
endif()

set(manifest_path "${SOURCE_ASSETS_DIR}/visuals.json")
if(NOT EXISTS "${manifest_path}")
    message(FATAL_ERROR "Missing assets manifest: ${manifest_path}")
endif()

file(READ "${manifest_path}" visuals_json)

# Match quoted relative paths to image/audio files inside the manifest.
set(relative_paths "")
string(REGEX MATCHALL "\"[^\"]+\\.(png|PNG|jpg|JPG|jpeg|JPEG|webp|WEBP|bmp|BMP|wav|WAV|ogg|OGG|flac|FLAC)\"" quoted_paths "${visuals_json}")
foreach(quoted_path IN LISTS quoted_paths)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" relative_path "${quoted_path}")
    list(APPEND relative_paths "${relative_path}")
endforeach()
list(REMOVE_DUPLICATES relative_paths)

if(relative_paths STREQUAL "")
    message(FATAL_ERROR "No texture paths found in ${manifest_path}")
endif()

# Drop stale unused copies from previous full-tree builds.
if(EXISTS "${DEST_ASSETS_DIR}")
    file(REMOVE_RECURSE "${DEST_ASSETS_DIR}")
endif()
file(MAKE_DIRECTORY "${DEST_ASSETS_DIR}")

file(COPY_FILE "${manifest_path}" "${DEST_ASSETS_DIR}/visuals.json" ONLY_IF_DIFFERENT)

foreach(relative_path IN LISTS relative_paths)
    set(source_file "${SOURCE_ASSETS_DIR}/${relative_path}")
    set(dest_file "${DEST_ASSETS_DIR}/${relative_path}")

    if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR "Runtime asset missing (listed in visuals.json): ${source_file}")
    endif()

    get_filename_component(dest_parent "${dest_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${dest_parent}")
    file(COPY_FILE "${source_file}" "${dest_file}" ONLY_IF_DIFFERENT)
endforeach()

# Cursor pack used by GameCursor (not referenced from visuals.json).
set(cursor_shape_folders 01 02 04 05 13 20 21 22 23 27 28)
foreach(shape_folder IN LISTS cursor_shape_folders)
    set(cursor_source_dir "${SOURCE_ASSETS_DIR}/textures/CursorCrystal/PNG/${shape_folder}")
    set(cursor_dest_dir "${DEST_ASSETS_DIR}/textures/CursorCrystal/PNG/${shape_folder}")
    if(EXISTS "${cursor_source_dir}")
        file(MAKE_DIRECTORY "${cursor_dest_dir}")
        file(GLOB cursor_pngs "${cursor_source_dir}/*.png")
        foreach(cursor_png IN LISTS cursor_pngs)
            get_filename_component(cursor_name "${cursor_png}" NAME)
            file(COPY_FILE "${cursor_png}" "${cursor_dest_dir}/${cursor_name}" ONLY_IF_DIFFERENT)
        endforeach()
    endif()
endforeach()

list(LENGTH relative_paths asset_count)
message(STATUS "Copied visuals.json + ${asset_count} used asset file(s) -> ${DEST_ASSETS_DIR}")
