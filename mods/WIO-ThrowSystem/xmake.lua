includes("lib/commonlibf4rd")

add_requires("glaze")

set_project("WIO-ThrowSystem")
set_version("2.0.1")
set_license("MIT")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("WIO-ThrowSystem")
    add_rules("commonlibf4rd.plugin")
    add_packages("glaze")
