const std = @import("std");

pub fn build(b: *std.build.Builder) !void {
    const target = std.zig.CrossTarget.parse(.{
        .arch_os_abi = "x86_64-windows-gnu"
    }) catch unreachable;

    // const mode = std.builtin.Mode.Debug;
    const mode = std.builtin.Mode.ReleaseFast;

    const imgui = b.addStaticLibrary(.{
        .name = "imgui",
        .target = target,
        .optimize = mode,
    });
    imgui.addCSourceFiles(&.{
        "./build/imgui/imgui.cpp",
        "./build/imgui/imgui_draw.cpp",
        "./build/imgui/imgui_tables.cpp",
        "./build/imgui/imgui_widgets.cpp",
        "./build/imgui/imgui_demo.cpp",
        "./build/imgui/misc/cpp/imgui_stdlib.cpp",
        "./build/imgui/backends/imgui_impl_glfw.cpp",
        "./build/imgui/backends/imgui_impl_opengl3.cpp",
    }, &.{
        "-std=c++11",
        // "-g",
    });
    imgui.addIncludePath("./build/imgui");
    imgui.addIncludePath("./build/glfw/include");
    imgui.linkLibC();
    imgui.linkLibCpp();
    imgui.linkSystemLibraryName("opengl32");

    const glfw = b.addStaticLibrary(.{
        .name = "glfw",
        .target = target,
        .optimize = mode,
    });
    glfw.defineCMacro("_GLFW_WIN32", null);

    glfw.linkLibC();
    glfw.linkSystemLibraryName("gdi32");

    const glfw_pathless_sources = [_][]const u8{
        "context.c",
        "init.c",
        "input.c",
        "monitor.c",
        "vulkan.c",
        "window.c",
        "win32_init.c",
        "win32_joystick.c",
        "win32_monitor.c",
        "win32_time.c",
        "win32_thread.c",
        "win32_window.c",
        "wgl_context.c",
        "egl_context.c",
        "osmesa_context.c",
    };

    var glfw_sources = std.ArrayList([] const u8).init(b.allocator);
    defer glfw_sources.deinit();
    for (glfw_pathless_sources) |source| {
        errdefer for (glfw_sources.items) |s| {
            b.allocator.free(s);
        };
        const path = try std.fmt.allocPrint(b.allocator, "./build/glfw/src/{s}", .{source});
        try glfw_sources.append(path);
    }
    defer for (glfw_sources.items) |source| {
        b.allocator.free(source);
    };

    glfw.addCSourceFiles(glfw_sources.items, &.{

    });

    const exe = b.addExecutable(.{
        .name = "YtDownloader",
        .target = target,
        .optimize = mode,
    });
    exe.addCSourceFile("./src/main.cpp", &.{
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        // "-g",
        // "-MJ", "build/compile_commands.json",
    });
    exe.addIncludePath("./build/imgui");
    exe.addIncludePath("./build/imgui/backends");
    exe.addIncludePath("./build/imgui/misc/cpp");
    exe.addIncludePath("./build/glfw/include");
    exe.addSystemIncludePath("./build/cpp-json/single_include");
    exe.linkLibC();
    exe.linkLibCpp();
    exe.linkSystemLibraryName("ole32");
    exe.linkSystemLibraryName("winhttp");
    exe.linkSystemLibraryName("shlwapi");
    exe.linkSystemLibraryName("api-ms-win-core-path-l1-1-0");
    exe.defineCMacro("INITGUID", null);

    // Temporary fix, since LTO is deleting _create_locale, needed by json
    exe.want_lto = false;

    exe.linkLibrary(imgui);
    exe.linkLibrary(glfw);

    exe.subsystem = std.Target.SubSystem.Windows;
    exe.install();

    const run_cmd = exe.run();
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
