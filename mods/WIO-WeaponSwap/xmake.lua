includes("lib/commonlibf4rd")

add_requires("glaze")

set_project("WIO-WeaponSwap")
set_version("1.0.0")
set_license("MIT")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("WIO-WeaponSwap")
    add_rules("commonlibf4rd.plugin")
    add_packages("glaze")
