from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# 1) SE 1.50: force shader uniforms to be re-pushed after a GL-context restart.
replace_once(
    "src/main/sdl2/rendersurface.hpp",
    "    long last_config           = 0;\n    int  last_vignette         = 0;",
    "    long last_config           = 0;\n    int  ticks                 = 3;   // reset on renderer init so a fresh GL program receives all uniforms\n    int  last_vignette         = 0;",
)

replace_once(
    "src/main/sdl2/rendersurface.cpp",
    "    FrameCounter = 0;\n    last_config  = 0;\n",
    "    FrameCounter = 0;\n    // A recreated GL program starts with zeroed uniforms. Force the first\n    // finalize_frame() after every (re)init to send the complete shader state.\n    last_config  = -1;\n    ticks        = 3;\n",
)

replace_once(
    "src/main/sdl2/rendersurface.cpp",
    "    static long last_config = 0;\n    static int  ticks = 3;\n    // check for any settings changes\n",
    "    // last_config/ticks are members reset by init(). Function-statics survive\n    // a GL-context teardown and can otherwise suppress the mandatory uniform\n    // upload against the newly linked shader program.\n    // check for any settings changes\n",
)


# 2) SE 1.50: do not allow the F7 hi-res-sprite mode while the engine is low-res.
replace_once(
    "src/main/sdl2/input_base.cpp",
    "        case SDLK_F7:\n            // JJP - switches between sprite rendering (original/hi-res)\n            if (!is_pressed) break;\n            config.video.hiresprites ^= 1;\n            break;",
    "        case SDLK_F7:\n            // Hi-res sprites rely on the hi-res engine coordinate/zoom path.\n            // Keep the hotkey consistent with the menu and reject the invalid\n            // low-res + hi-res-sprites combination.\n            if (!is_pressed) break;\n            if (config.video.hires == 0) break;\n            config.video.hiresprites ^= 1;\n            break;",
)


# 3) SE 1.50 idea adapted for DX: generate VERSIONINFO from the DX version macro.
replace_once(
    "CMakeLists.txt",
    "if(WIN32)\n    list(APPEND src_directx\n        \"${CMAKE_SOURCE_DIR}/res/cannonball.rc\"\n    )\nendif()",
    '''if(WIN32)\n    # Keep Explorer's executable metadata tied to the same version string the\n    # game itself reports. Do not duplicate the DX version in the .rc file.\n    file(READ \"${main_cpp_base}/globals.hpp\" _cb_globals_hpp)\n    string(REGEX MATCH \"CANNONBALL_DX_VERSION[ \\t]+\\\"([^\\\"]+)\\\"\" _cb_unused \"${_cb_globals_hpp}\")\n    if(NOT CMAKE_MATCH_1)\n        message(FATAL_ERROR \"Could not parse CANNONBALL_DX_VERSION from globals.hpp\")\n    endif()\n    set(CB_VERSION_STRING \"${CMAKE_MATCH_1}\")\n\n    string(REPLACE \".\" \";\" _cb_version_parts \"${CB_VERSION_STRING}\")\n    list(LENGTH _cb_version_parts _cb_version_count)\n    list(GET _cb_version_parts 0 CB_VERSION_MAJOR)\n    if(_cb_version_count GREATER 1)\n        list(GET _cb_version_parts 1 CB_VERSION_MINOR)\n    else()\n        set(CB_VERSION_MINOR 0)\n    endif()\n    if(_cb_version_count GREATER 2)\n        list(GET _cb_version_parts 2 CB_VERSION_PATCH)\n    else()\n        set(CB_VERSION_PATCH 0)\n    endif()\n\n    set(CB_ICON_PATH \"${CMAKE_SOURCE_DIR}/res/cannonball.ico\")\n    configure_file(\n        \"${CMAKE_SOURCE_DIR}/res/cannonball.rc.in\"\n        \"${CMAKE_CURRENT_BINARY_DIR}/cannonball.rc\"\n        @ONLY\n    )\n    message(STATUS \"CannonBall DX version ${CB_VERSION_STRING} (resource ${CB_VERSION_MAJOR},${CB_VERSION_MINOR},${CB_VERSION_PATCH},0)\")\n\n    list(APPEND src_directx\n        \"${CMAKE_CURRENT_BINARY_DIR}/cannonball.rc\"\n    )\nendif()''',
)

Path("res/cannonball.rc.in").write_text(r'''#include <winver.h>

IDI_ICON1 ICON DISCARDABLE "@CB_ICON_PATH@"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION    @CB_VERSION_MAJOR@,@CB_VERSION_MINOR@,@CB_VERSION_PATCH@,0
 PRODUCTVERSION @CB_VERSION_MAJOR@,@CB_VERSION_MINOR@,@CB_VERSION_PATCH@,0
 FILEFLAGSMASK  VS_FFI_FILEFLAGSMASK
#ifdef _DEBUG
 FILEFLAGS      VS_FF_DEBUG
#else
 FILEFLAGS      0x0L
#endif
 FILEOS         VOS_NT_WINDOWS32
 FILETYPE       VFT_APP
 FILESUBTYPE    VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "080904B0"
        BEGIN
            VALUE "CompanyName",      "CannonBall DX Project"
            VALUE "FileDescription",  "CannonBall DX - enhanced OutRun engine"
            VALUE "FileVersion",      "@CB_VERSION_STRING@"
            VALUE "InternalName",     "cannonball-dx"
            VALUE "LegalCopyright",   "Based on CannonBall by Chris White and CannonBall-SE by James Pearce."
            VALUE "OriginalFilename", "cannonball-dx.exe"
            VALUE "ProductName",      "CannonBall DX"
            VALUE "ProductVersion",   "@CB_VERSION_STRING@"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x809, 1200
    END
END
''', encoding="utf-8")

old_rc = Path("res/cannonball.rc")
if old_rc.exists():
    old_rc.unlink()

print("SE 1.50 stage 1 selective ports applied successfully")
