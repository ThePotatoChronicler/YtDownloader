const h = @cImport(@cInclude("i18n.h"));
const std = @import("std");
const builtin = @import("builtin");
const mem = std.mem;
const testing = std.testing;
const debug = std.debug;
const assert = std.debug.assert;
const i18n_build = @import("i18n_build");
const heap = std.heap;

const Language = struct {
    name: []const u8,
    formalname: []const u8,
};

const languages = blk: {
    const langs: []const []const u8 = i18n_build.langs;
    const formalnames: []const []const u8 = i18n_build.formalnames;

    var buf: [langs.len]Language = undefined;

    for (&buf, langs, formalnames) |*language, name, formalname| {
        language.* = Language{
            .name = name,
            .formalname = formalname,
        };
    }

    break :blk buf;
};

var phrases: [languages.len]std.StringHashMap([]const u8) = undefined;

const error_default = "!NULL!";

export fn YD_init_translations() c_int {
    inline for (languages, 0..) |lang, i| {
        var map = std.StringHashMap([]const u8).init(heap.c_allocator);
        var keys: []const []const u8 = @field(i18n_build, "phrases_" ++ lang.name ++ "_keys");
        var vals: []const []const u8 = @field(i18n_build, "phrases_" ++ lang.name ++ "_vals");

        for (keys, vals) |k, v| {
            map.put(k, v) catch return -1;
        }

        phrases[i] = map;
    }

    return 0;
}

export fn YD_deinit_translations() void {
    for (&phrases) |*p| {
        p.deinit();
    }
}

export fn YD_get_phrase_str(langstr: [*c]const u8, id: [*c]const u8) [*c]const u8 {
    assert(langstr != null);
    assert(id != null);

    for (languages, 0..) |lang, i| {
        if (!eqlZigstrToCstr(lang.name, langstr)) {
            continue;
        }

        if (phrases[i].get(mem.span(id))) |phrase| {
            return phrase.ptr;
        } else {
            if (builtin.mode == .Debug) {
                std.debug.print("Missing translation - lang:{s} id:{s}\n", .{ langstr, id });
            }
        }
    }

    return error_default;
}

export fn YD_language_to_formal_str(lang: [*c]const u8) [*c]const u8 {
    assert(lang != null);

    for (languages) |language| {
        if (eqlZigstrToCstr(language.name, lang)) {
            return language.formalname.ptr;
        }
    }

    return null;
}

fn eqlZigstrToCstr(zigstr: []const u8, cstr: [*c]const u8) bool {
    debug.assert(cstr != null);

    var i: u32 = 0;
    while (true) : ({
        i += 1;
    }) {
        if (cstr[i] == 0) {
            if (zigstr.len > i) {
                return false;
            }
            return true;
        }

        if (zigstr[i] != cstr[i]) {
            return false;
        }
    }
}

export const YD_languages_list: [*c][*c]const u8 = &blk: {
    var buf: [languages.len][*c]const u8 = undefined;
    for (&buf, languages) |*l, lang| {
        l.* = lang.name.ptr;
    }
    break :blk buf;
};
export const YD_languages_list_len: c_int = languages.len;
