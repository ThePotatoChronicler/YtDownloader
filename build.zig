const std = @import("std");
const Build = std.Build;
const Step = std.build.Step;
const Manifest = Build.Cache.Manifest;
const fs = std.fs;
const mem = std.mem;
const compress = std.compress;
const io = std.io;
const math = std.math;
const LazyPath = std.Build.LazyPath;
const json = std.json;
const ArrayList = std.ArrayList;
const process = std.process;

const cpp_source_files = .{
    "./src/main.cpp",
    "./src/utils.cpp",
    "./src/download.cpp",
};

const compiler_flags = .{
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-Wshadow",
    // "-g",
};

const JsonLanguage = struct {
    lang: []const u8,
    formalname: []const u8,
    phrases: json.ArrayHashMap([]const u8),
};

pub fn build(b: *Build) !void {
    // It barely works, so we're just waiting for zig to fix it
    const doGenerateCompileCommands = b.option(bool, "generate-ccjson", "Generate compile_commands.json (experimental)") orelse false;

    const target = std.zig.CrossTarget.parse(.{ .arch_os_abi = "x86_64-windows-gnu" }) catch unreachable;
    const optimize = b.standardOptimizeOption(.{});

    const glfw_dep = b.dependency("glfw", .{});
    const glfw = b.addStaticLibrary(.{
        .name = "glfw",
        .target = target,
        .optimize = optimize,
    });
    glfw.defineCMacro("_GLFW_WIN32", null);

    glfw.linkLibC();
    glfw.linkSystemLibraryName("gdi32");
    for (glfw_source_files) |file| {
        glfw.addCSourceFile(.{
            .file = glfw_dep.path(file),
            .flags = &.{},
        });
    }

    const imgui_dep = b.dependency("imgui", .{});
    const imgui = b.addStaticLibrary(.{
        .name = "imgui",
        .target = target,
        .optimize = optimize,
    });

    for (imgui_source_files) |file| {
        imgui.addCSourceFile(.{
            .file = imgui_dep.path(file),
            .flags = &.{
                "-std=c++11",
            },
        });
    }
    imgui.addIncludePath(imgui_dep.path(""));
    imgui.addIncludePath(glfw_dep.path("include"));
    imgui.linkLibC();
    imgui.linkLibCpp();
    imgui.linkSystemLibraryName("opengl32");

    var compilation_args = std.ArrayList([]const u8).init(b.allocator);
    try compilation_args.appendSlice(&compiler_flags);

    const i18n = b.addStaticLibrary(.{
        .name = "i18n",
        .target = target,
        .optimize = optimize,
        .root_source_file = LazyPath.relative("src/i18n.zig"),
    });

    i18n.linkLibC();
    i18n.addIncludePath(LazyPath.relative("src"));

    const i18n_build = b.addOptions();
    i18n.addOptions("i18n_build", i18n_build);

    const translation_texts = try getTranslationTexts(b);
    defer {
        for (translation_texts.items) |i| {
            b.allocator.free(i);
        }
        translation_texts.deinit();
    }

    try fillTranslationOptions(i18n_build, translation_texts.items);

    const cpp_json_dep = b.dependency("cpp-json", .{});

    const exe = b.addExecutable(.{
        .name = "YtDownloader",
        .target = target,
        .optimize = optimize,
    });
    exe.addIncludePath(imgui_dep.path(""));
    exe.addIncludePath(imgui_dep.path("backends"));
    exe.addIncludePath(imgui_dep.path("misc/cpp"));
    exe.addIncludePath(glfw_dep.path("include"));
    exe.addSystemIncludePath(cpp_json_dep.path("single_include"));
    exe.linkLibC();
    exe.linkLibCpp();
    exe.linkSystemLibraryName("ole32");
    exe.linkSystemLibraryName("winhttp");
    exe.linkSystemLibraryName("shlwapi");
    exe.linkSystemLibraryName("api-ms-win-core-path-l1-1-0");
    exe.defineCMacro("INITGUID", null);

    if (optimize == .Debug) {
        exe.defineCMacro("JSON_DIAGNOSTICS", null);
    }

    // Temporary fix, since LTO is deleting _create_locale, needed by cpp-json
    exe.want_lto = false;

    exe.addIncludePath(LazyPath.relative("src"));

    exe.linkLibrary(imgui);
    exe.linkLibrary(glfw);
    exe.linkLibrary(i18n);

    exe.subsystem = std.Target.SubSystem.Windows;

    if (doGenerateCompileCommands) {
        var genstep = try CompilationDatabaseStep.initAlloc(b, exe, &cpp_source_files, &compiler_flags);

        // FIXME This doesn't consistently check if the files exist and creates them, so deleting
        //       the build file can result in this not creating a new one until something changes the manifest
        if (!genstep.cached) {
            try compilation_args.appendSlice(&.{ "-gen-cdb-fragment-path", genstep.cdb_dir_path });
            genstep.step.dependOn(&exe.step);
            b.default_step.dependOn(&genstep.step);
        }
    }

    exe.addCSourceFiles(&cpp_source_files, compilation_args.items);

    const install_artifact = b.installArtifact(exe);
    _ = install_artifact;

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}

const CompilationDatabaseStep = struct {
    step: Step,
    cached: bool,
    hash: [Build.Cache.hex_digest_len]u8,
    cdb_dir_path: []u8,
    manifest: Manifest,

    const Self = @This();

    pub fn initAlloc(b: *Build, exe: *Step.Compile, source_files: []const []const u8, comp_flags: []const []const u8) !*Self {
        var manifest = b.cache.obtain();

        // FIXME: This isn't nearly enough to consistently cache builds, we need
        //        to also cache the dependencies and their flags and includes
        //        for this to be completely consistent, or atleast consistent enough
        manifest.hash.add(@as(u32, 0xad92deab));
        try manifest.addListOfFiles(source_files);
        manifest.hash.addListOfBytes(comp_flags);

        try addCompileStepToManifest(&manifest, exe, b);

        const cached = try manifest.hit();
        const hash = manifest.final();

        const cache_path = b.cache_root.path orelse try fs.cwd().realpathAlloc(b.allocator, ".");

        const cdb_dir_path = b.pathFromRoot(b.pathJoin(&.{ cache_path, "cdb", &hash }));

        try b.cache_root.handle.makePath("cdb");

        const allocated = try b.allocator.create(Self);
        allocated.* = .{
            .step = Step.init(.{
                .id = .custom,
                .owner = b,
                .name = "Stitch together compile_commands.json",
                .makeFn = make,
            }),
            .cached = cached,
            .hash = hash,
            .cdb_dir_path = cdb_dir_path,
            .manifest = manifest,
        };
        return allocated;
    }

    fn addCompileStepToManifest(manifest: *Manifest, exe: *Step.Compile, b: *Build) !void {
        // FIXME This isn't nearly enough to consistently cache files,
        //       for example, compiler flags aren't cached.

        for (exe.c_macros.items) |macro| {
            manifest.hash.add(@as(u32, 0x92197e58));
            manifest.hash.addBytes(macro);
        }

        for (exe.include_dirs.items) |incl_dir| {
            switch (incl_dir) {
                .path => |p| {
                    manifest.hash.add(@as(u32, 0x4dfe76cb));
                    manifest.hash.addBytes(p.getPath(b));
                },
                .path_system => |p| {
                    manifest.hash.add(@as(u32, 0xe31c9fb0));
                    manifest.hash.addBytes(p.getPath(b));
                },
                .framework_path => |p| {
                    manifest.hash.add(@as(u32, 0x06ec1b92));
                    manifest.hash.addBytes(p.getPath(b));
                },
                .framework_path_system => |p| {
                    manifest.hash.add(@as(u32, 0x9f63b83f));
                    manifest.hash.addBytes(p.getPath(b));
                },
                .other_step => |s| {
                    manifest.hash.add(@as(u32, 0x343f7121));
                    try addCompileStepToManifest(manifest, s, b);
                    manifest.hash.add(@as(u32, 0x7e56bbd1));
                },
                .config_header_step => |_| {
                    @panic("Unimplemented");
                },
                .path_after => |p| {
                    manifest.hash.add(@as(u32, 0x097c4d6b));
                    manifest.hash.addBytes(p.getPath(b));
                },
            }
        }
    }

    fn make(step: *Step, prog_node: *std.Progress.Node) !void {
        _ = prog_node;

        const self = @fieldParentPtr(Self, "step", step);
        defer self.manifest.deinit();

        const cfjson_path = step.owner.pathFromRoot("build/compile_commands.json");
        const exists = exists: {
            fs.accessAbsolute(cfjson_path, .{}) catch |err| {
                if (err == error.FileNotFound) {
                    break :exists false;
                }
                return err;
            };
            break :exists true;
        };

        step.result_cached = self.cached and exists;

        if (step.result_cached) {
            return;
        }

        fs.makeDirAbsolute(step.owner.pathFromRoot("build")) catch |err| {
            if (err != error.PathAlreadyExists) {
                return err;
            }
        };
        const cfjson_file = try fs.createFileAbsolute(cfjson_path, .{});
        defer cfjson_file.close();

        const json_writer = cfjson_file.writer();
        try json_writer.writeByte('[');

        var cdb_dir = fs.openIterableDirAbsolute(self.cdb_dir_path, .{}) catch |err| {
            return step.fail("Cannot open CDB directory '{s}': {s}", .{ self.cdb_dir_path, @errorName(err) });
        };
        defer cdb_dir.close();

        var cdb_iter = cdb_dir.iterate();

        var first = true;

        while (try cdb_iter.next()) |entry| {
            if (entry.kind != .file) {
                return step.fail("Expected a file, found {s}", .{@tagName(entry.kind)});
            }

            if (first) {
                first = false;
            } else {
                try json_writer.writeByte(',');
            }

            const file = try cdb_dir.dir.openFile(entry.name, .{});
            const file_content = try file.readToEndAlloc(step.owner.allocator, math.maxInt(usize));
            defer step.owner.allocator.free(file_content);

            try json_writer.writeAll(file_content[0 .. file_content.len - 2]);
        }

        try json_writer.writeByte(']');

        try self.manifest.writeManifest();
    }
};

fn getTranslationTexts(b: *Build) !std.ArrayList([]const u8) {
    var translation = try b.build_root.handle.openIterableDir("translation", .{});
    var walker = try translation.walk(b.allocator);
    defer walker.deinit();

    var texts = std.ArrayList([]const u8).init(b.allocator);
    errdefer {
        for (texts.items) |i| {
            b.allocator.free(i);
        }
        texts.deinit();
    }

    while (try walker.next()) |entry| {
        if (entry.kind != .file) {
            continue;
        }

        if (!mem.endsWith(u8, entry.path, ".json")) {
            continue;
        }

        // std.debug.print("Parsing {s}\n", .{entry.basename});

        var file = try entry.dir.openFile(entry.basename, .{});
        defer file.close();

        var jsontext = try file.readToEndAlloc(b.allocator, math.pow(usize, 2, 16));
        try texts.append(jsontext);
    }

    return texts;
}

fn fillTranslationOptions(options: *Build.Step.Options, translation_texts: []const []const u8) !void {
    const b = options.step.owner;
    const allocator = b.allocator;

    var langobjects = ArrayList(json.Parsed(JsonLanguage)).init(allocator);
    defer {
        for (langobjects.items) |obj| {
            obj.deinit();
        }
        langobjects.deinit();
    }

    var langs = ArrayList([]const u8).init(allocator);
    defer langs.deinit();

    var formalnames = ArrayList([]const u8).init(allocator);
    defer formalnames.deinit();

    for (translation_texts) |text| {
        // TODO Log syntax errors with filename and/or path
        const lang = try json.parseFromSlice(JsonLanguage, allocator, text, .{});

        try langobjects.append(lang);
        try langs.append(lang.value.lang);
        try formalnames.append(lang.value.formalname);

        options.addOption([]const []const u8, b.fmt("phrases_{s}_keys", .{lang.value.lang}), lang.value.phrases.map.keys());
        options.addOption([]const []const u8, b.fmt("phrases_{s}_vals", .{lang.value.lang}), lang.value.phrases.map.values());
    }

    options.addOption([]const []const u8, "langs", langs.items);
    options.addOption([]const []const u8, "formalnames", formalnames.items);
}

const imgui_source_files = [_][]const u8{
    "imgui.cpp",
    "imgui_draw.cpp",
    "imgui_tables.cpp",
    "imgui_widgets.cpp",
    "imgui_demo.cpp",
    "misc/cpp/imgui_stdlib.cpp",
    "backends/imgui_impl_glfw.cpp",
    "backends/imgui_impl_opengl3.cpp",
};

const glfw_source_files = [_][]const u8{
    "src/context.c",
    "src/init.c",
    "src/input.c",
    "src/monitor.c",
    "src/vulkan.c",
    "src/window.c",
    "src/win32_init.c",
    "src/win32_joystick.c",
    "src/win32_monitor.c",
    "src/win32_time.c",
    "src/win32_thread.c",
    "src/win32_window.c",
    "src/wgl_context.c",
    "src/egl_context.c",
    "src/osmesa_context.c",
};
