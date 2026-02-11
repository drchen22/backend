add_rules("mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "./" })
set_languages("c++23")
add_requires("catch2")

target("backend")
set_kind("static")
add_files("server/source/**.cpp")
add_includedirs("server/include", { public = true })


target("main")
set_kind("binary")
add_files("main.cpp")
add_deps("backend")

target("tests")
set_kind("binary")
add_files("tests/**.cpp")
add_deps("backend")
add_packages("catch2")

task("test")
    set_menu {
        usage = "xmake test",
        description = "Run all tests"
    }
    on_run(function ()
        os.exec("xmake build tests")
        os.exec("xmake run tests")
    end)