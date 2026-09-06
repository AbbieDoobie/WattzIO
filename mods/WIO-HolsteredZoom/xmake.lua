includes("lib/commonlibf4rd")

set_project("WIO-HolsteredZoom")
set_version("1.0.1")
set_license("MIT")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("WIO-HolsteredZoom")
    add_rules("commonlibf4rd.plugin")
