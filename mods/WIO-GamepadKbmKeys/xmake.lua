includes("lib/commonlibf4rd")

set_project("WIO-GamepadKbmKeys")
set_version("1.0.0")
set_license("MIT")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("WIO-GamepadKbmKeys")
    add_rules("commonlibf4rd.plugin")
