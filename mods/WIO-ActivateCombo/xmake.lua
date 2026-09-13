includes("lib/commonlibf4rd")

add_requires("glaze")

set_project("WIO-ActivateCombo")
set_version("1.1.0")
set_license("MIT")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("WIO-ActivateCombo")
    add_rules("commonlibf4rd.plugin")
    add_packages("glaze")
