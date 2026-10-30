/*
  Native File Dialog Extended
  Repository: https://github.com/btzy/nativefiledialog-extended
  License: Zlib
  Authors: Bernard Teo, Michael Labbe

  Note: We do not check for malloc failure on Linux - Linux overcommits memory!
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <dlfcn.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <string>
#include <vector>

#include "nfd.h"

/*
Define NFD_CASE_SENSITIVE_FILTER if you want file filters to be case-sensitive.  The default
is case-insensitive.  While Linux uses a case-sensitive filesystem and is designed for
case-sensitive file extensions, perhaps in the vast majority of cases users actually expect the file
filters to be case-insensitive.
*/

namespace {

template <typename T>
struct Free_Guard {
    T* data;
    Free_Guard(T* freeable) noexcept : data(freeable) {}
    ~Free_Guard() { NFDi_Free(data); }
};

/* current error */
const char* g_errorstr = nullptr;

void NFDi_SetError(const char* msg) {
    g_errorstr = msg;
}

template <typename T = void>
T* NFDi_Malloc(size_t bytes) {
    void* ptr = malloc(bytes);
    if (!ptr) NFDi_SetError("NFDi_Malloc failed.");

    return static_cast<T*>(ptr);
}

template <typename T>
void NFDi_Free(T* ptr) {
    assert(ptr);
    free(static_cast<void*>(ptr));
}

template <typename T>
T* copy(const T* begin, const T* end, T* out) {
    for (; begin != end; ++begin) {
        *out++ = *begin;
    }
    return out;
}

#ifndef NFD_CASE_SENSITIVE_FILTER
nfdnchar_t* emit_case_insensitive_glob(const nfdnchar_t* begin,
                                       const nfdnchar_t* end,
                                       nfdnchar_t* out) {
    // this code will only make regular Latin characters case-insensitive; other
    // characters remain case sensitive
    for (; begin != end; ++begin) {
        if ((*begin >= 'A' && *begin <= 'Z') || (*begin >= 'a' && *begin <= 'z')) {
            *out++ = '[';
            *out++ = *begin;
            // invert the case of the original character
            *out++ = *begin ^ static_cast<nfdnchar_t>(0x20);
            *out++ = ']';
        } else {
            *out++ = *begin;
        }
    }
    return out;
}
#endif

// Does not own the filter and extension.
struct Pair_GtkFileFilter_FileExtension {
    GtkFileFilter* filter;
    const nfdnchar_t* extensionBegin;
    const nfdnchar_t* extensionEnd;
};

void AddFiltersToDialog(GtkFileChooser* chooser,
                        const nfdnfilteritem_t* filterList,
                        nfdfiltersize_t filterCount) {
    if (filterCount) {
        assert(filterList);

        // we have filters to add ... format and add them

        for (nfdfiltersize_t index = 0; index != filterCount; ++index) {
            GtkFileFilter* filter = gtk_file_filter_new();

            // count number of file extensions
            size_t sep = 1;
            for (const nfdnchar_t* p_spec = filterList[index].spec; *p_spec; ++p_spec) {
                if (*p_spec == ',') {
                    ++sep;
                }
            }

            // friendly name conversions: "png,jpg" -> "Image files
            // (png, jpg)"

            // calculate space needed (including the trailing '\0')
            size_t nameSize =
                sep + strlen(filterList[index].spec) + 3 + strlen(filterList[index].name);

            // malloc the required memory
            nfdnchar_t* nameBuf = NFDi_Malloc<nfdnchar_t>(sizeof(nfdnchar_t) * nameSize);

            nfdnchar_t* p_nameBuf = nameBuf;
            for (const nfdnchar_t* p_filterName = filterList[index].name; *p_filterName;
                 ++p_filterName) {
                *p_nameBuf++ = *p_filterName;
            }
            *p_nameBuf++ = ' ';
            *p_nameBuf++ = '(';
            const nfdnchar_t* p_extensionStart = filterList[index].spec;
            for (const nfdnchar_t* p_spec = filterList[index].spec; true; ++p_spec) {
                if (*p_spec == ',' || !*p_spec) {
                    if (*p_spec == ',') {
                        *p_nameBuf++ = ',';
                        *p_nameBuf++ = ' ';
                    }

#ifdef NFD_CASE_SENSITIVE_FILTER
                    // +1 for the trailing '\0'
                    nfdnchar_t* extnBuf = NFDi_Malloc<nfdnchar_t>(sizeof(nfdnchar_t) *
                                                                  (p_spec - p_extensionStart + 3));
                    nfdnchar_t* p_extnBufEnd = extnBuf;
                    *p_extnBufEnd++ = '*';
                    *p_extnBufEnd++ = '.';
                    p_extnBufEnd = copy(p_extensionStart, p_spec, p_extnBufEnd);
                    *p_extnBufEnd++ = '\0';
                    gtk_file_filter_add_pattern(filter, extnBuf);
                    NFDi_Free(extnBuf);
#else
                    // Each character in the Latin alphabet is converted into 4 characters.  E.g.
                    // 'a' is converted into "[Aa]".  Other characters are preserved.  Then we +1
                    // for the trailing '\0'.
                    nfdnchar_t* extnBuf = NFDi_Malloc<nfdnchar_t>(
                        sizeof(nfdnchar_t) * ((p_spec - p_extensionStart) * 4 + 3));
                    nfdnchar_t* p_extnBufEnd = extnBuf;
                    *p_extnBufEnd++ = '*';
                    *p_extnBufEnd++ = '.';
                    p_extnBufEnd =
                        emit_case_insensitive_glob(p_extensionStart, p_spec, p_extnBufEnd);
                    *p_extnBufEnd++ = '\0';
                    gtk_file_filter_add_pattern(filter, extnBuf);
                    NFDi_Free(extnBuf);
#endif

                    if (*p_spec) {
                        // update the extension start point
                        p_extensionStart = p_spec + 1;
                    } else {
                        // reached the '\0' character
                        break;
                    }
                } else {
                    *p_nameBuf++ = *p_spec;
                }
            }
            *p_nameBuf++ = ')';
            *p_nameBuf++ = '\0';
            assert((size_t)(p_nameBuf - nameBuf) == sizeof(nfdnchar_t) * nameSize);

            // add to the filter
            gtk_file_filter_set_name(filter, nameBuf);

            // free the memory
            NFDi_Free(nameBuf);

            // add filter to chooser
            gtk_file_chooser_add_filter(chooser, filter);
        }
    }

    /* always append a wildcard option to the end*/

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(chooser, filter);
}

// returns null-terminated map (trailing .filter is null)
Pair_GtkFileFilter_FileExtension* AddFiltersToDialogWithMap(GtkFileChooser* chooser,
                                                            const nfdnfilteritem_t* filterList,
                                                            nfdfiltersize_t filterCount) {
    Pair_GtkFileFilter_FileExtension* map = NFDi_Malloc<Pair_GtkFileFilter_FileExtension>(
        sizeof(Pair_GtkFileFilter_FileExtension) * (filterCount + 1));

    if (filterCount) {
        assert(filterList);

        // we have filters to add ... format and add them

        for (nfdfiltersize_t index = 0; index != filterCount; ++index) {
            GtkFileFilter* filter = gtk_file_filter_new();

            // store filter in map
            map[index].filter = filter;
            map[index].extensionBegin = filterList[index].spec;
            map[index].extensionEnd = nullptr;

            // count number of file extensions
            size_t sep = 1;
            for (const nfdnchar_t* p_spec = filterList[index].spec; *p_spec; ++p_spec) {
                if (*p_spec == ',') {
                    ++sep;
                }
            }

            // friendly name conversions: "png,jpg" -> "Image files
            // (png, jpg)"

            // calculate space needed (including the trailing '\0')
            size_t nameSize =
                sep + strlen(filterList[index].spec) + 3 + strlen(filterList[index].name);

            // malloc the required memory
            nfdnchar_t* nameBuf = NFDi_Malloc<nfdnchar_t>(sizeof(nfdnchar_t) * nameSize);

            nfdnchar_t* p_nameBuf = nameBuf;
            for (const nfdnchar_t* p_filterName = filterList[index].name; *p_filterName;
                 ++p_filterName) {
                *p_nameBuf++ = *p_filterName;
            }
            *p_nameBuf++ = ' ';
            *p_nameBuf++ = '(';
            const nfdnchar_t* p_extensionStart = filterList[index].spec;
            for (const nfdnchar_t* p_spec = filterList[index].spec; true; ++p_spec) {
                if (*p_spec == ',' || !*p_spec) {
                    if (*p_spec == ',') {
                        *p_nameBuf++ = ',';
                        *p_nameBuf++ = ' ';
                    }

#ifdef NFD_CASE_SENSITIVE_FILTER
                    // +1 for the trailing '\0'
                    nfdnchar_t* extnBuf = NFDi_Malloc<nfdnchar_t>(sizeof(nfdnchar_t) *
                                                                  (p_spec - p_extensionStart + 3));
                    nfdnchar_t* p_extnBufEnd = extnBuf;
                    *p_extnBufEnd++ = '*';
                    *p_extnBufEnd++ = '.';
                    p_extnBufEnd = copy(p_extensionStart, p_spec, p_extnBufEnd);
                    *p_extnBufEnd++ = '\0';
                    gtk_file_filter_add_pattern(filter, extnBuf);
                    NFDi_Free(extnBuf);
#else
                    // Each character in the Latin alphabet is converted into 4 characters.  E.g.
                    // 'a' is converted into "[Aa]".  Other characters are preserved.  Then we +1
                    // for the trailing '\0'.
                    nfdnchar_t* extnBuf = NFDi_Malloc<nfdnchar_t>(
                        sizeof(nfdnchar_t) * ((p_spec - p_extensionStart) * 4 + 3));
                    nfdnchar_t* p_extnBufEnd = extnBuf;
                    *p_extnBufEnd++ = '*';
                    *p_extnBufEnd++ = '.';
                    p_extnBufEnd =
                        emit_case_insensitive_glob(p_extensionStart, p_spec, p_extnBufEnd);
                    *p_extnBufEnd++ = '\0';
                    gtk_file_filter_add_pattern(filter, extnBuf);
                    NFDi_Free(extnBuf);
#endif

                    // store current pointer in map (if it's
                    // the first one)
                    if (map[index].extensionEnd == nullptr) {
                        map[index].extensionEnd = p_spec;
                    }

                    if (*p_spec) {
                        // update the extension start point
                        p_extensionStart = p_spec + 1;
                    } else {
                        // reached the '\0' character
                        break;
                    }
                } else {
                    *p_nameBuf++ = *p_spec;
                }
            }
            *p_nameBuf++ = ')';
            *p_nameBuf++ = '\0';
            assert((size_t)(p_nameBuf - nameBuf) == sizeof(nfdnchar_t) * nameSize);

            // add to the filter
            gtk_file_filter_set_name(filter, nameBuf);

            // free the memory
            NFDi_Free(nameBuf);

            // add filter to chooser
            gtk_file_chooser_add_filter(chooser, filter);
        }
    }
    // set trailing map index to null
    map[filterCount].filter = nullptr;

    /* always append a wildcard option to the end*/
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(chooser, filter);

    return map;
}

void SetDefaultPath(GtkFileChooser* chooser, const char* defaultPath) {
    if (!defaultPath || !*defaultPath) return;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(defaultPath);
    if (fs::is_regular_file(p, ec)) {
        p = p.parent_path();
    }
    while (!p.empty()) {
        ec.clear();
        if (fs::is_directory(p, ec) && !ec) {
            gtk_file_chooser_set_current_folder(chooser, p.c_str());
            return;
        }
        const fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
}

void SetDefaultName(GtkFileChooser* chooser, const char* defaultName) {
    if (!defaultName || !*defaultName) return;

    gtk_file_chooser_set_current_name(chooser, defaultName);
}

void WaitForCleanup() {
    while (gtk_events_pending()) gtk_main_iteration();
}

struct NativeDialog_Guard {
    GtkFileChooserNative* data;
    explicit NativeDialog_Guard(GtkFileChooserNative* dlg) noexcept : data(dlg) {}
    ~NativeDialog_Guard() {
        if (data) {
            WaitForCleanup();
            gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(data));
            g_object_unref(data);
            WaitForCleanup();
        }
    }
};

bool HasCompiledSchemas(const std::filesystem::path& schemaDir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(schemaDir / "gschemas.compiled", ec) && !ec;
}

void PrependXdgDataDir(const std::filesystem::path& shareDir) {
    const std::string dirStr = shareDir.string();
    if (dirStr.empty()) return;

    const char* cur = getenv("XDG_DATA_DIRS");
    if (cur && *cur) {
        const std::string curStr(cur);
        // Check colon-delimited token match
        size_t pos = 0;
        while (pos <= curStr.size()) {
            const size_t next = curStr.find(':', pos);
            const std::string token =
                next == std::string::npos ? curStr.substr(pos) : curStr.substr(pos, next - pos);
            if (token == dirStr) return;
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        const std::string updated = dirStr + ":" + curStr;
        setenv("XDG_DATA_DIRS", updated.c_str(), 1);
    } else {
        const std::string updated = dirStr + ":/usr/local/share:/usr/share";
        setenv("XDG_DATA_DIRS", updated.c_str(), 1);
    }
}

void RegisterSchemaDir(const std::filesystem::path& shareRoot,
                       const std::filesystem::path& schemaDir) {
    if (!HasCompiledSchemas(schemaDir)) return;
    PrependXdgDataDir(shareRoot);
    const char* curSchemaEnv = getenv("GSETTINGS_SCHEMA_DIR");
    if (!curSchemaEnv || !*curSchemaEnv || !HasCompiledSchemas(curSchemaEnv)) {
        setenv("GSETTINGS_SCHEMA_DIR", schemaDir.c_str(), 1);
    }
}

void RegisterSchemaPrefix(const std::filesystem::path& prefix) {
    if (prefix.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(prefix, ec) || ec) return;

    RegisterSchemaDir(prefix / "share", prefix / "share" / "glib-2.0" / "schemas");

    const fs::path gsRoot = prefix / "share" / "gsettings-schemas";
    ec.clear();
    if (fs::is_directory(gsRoot, ec) && !ec) {
        for (fs::directory_iterator it(gsRoot, ec), end; it != end && !ec; it.increment(ec)) {
            if (it->is_directory(ec) && !ec) {
                RegisterSchemaDir(it->path(), it->path() / "glib-2.0" / "schemas");
            }
        }
    }
}

void RegisterLibrarySchemaPrefix(void* symbolAddr) {
    Dl_info info{};
    if (dladdr(symbolAddr, &info) && info.dli_fname && *info.dli_fname) {
        const std::filesystem::path libPath(info.dli_fname);
        // e.g. /nix/store/<hash>-gtk+3-3.24.52/lib/libgtk-3.so.0 -> prefix
        RegisterSchemaPrefix(libPath.parent_path().parent_path());
    }
}

void EnsureGtkDialogEnvironment() noexcept {
    static bool configured = false;
    if (configured) return;
    configured = true;

    // Prefer xdg-desktop-portal (org.freedesktop.portal.FileChooser) so file,
    // folder, and Save As dialogs use the native desktop picker when available.
    // Respects any explicit caller override (overwrite = 0).
    setenv("GTK_USE_PORTAL", "1", 0);

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path selfExe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !selfExe.empty()) {
        const fs::path exeDir = selfExe.parent_path();
        RegisterSchemaPrefix(exeDir);
        RegisterSchemaPrefix(exeDir.parent_path());
    }

    RegisterLibrarySchemaPrefix(reinterpret_cast<void*>(&gtk_init_check));
    RegisterLibrarySchemaPrefix(reinterpret_cast<void*>(&g_settings_schema_source_get_default));

    RegisterSchemaPrefix("/run/current-system/sw");
    if (const char* user = getenv("USER"); user && *user) {
        RegisterSchemaPrefix(fs::path("/etc/profiles/per-user") / user);
    }
    if (const char* home = getenv("HOME"); home && *home) {
        RegisterSchemaPrefix(fs::path(home) / ".nix-profile");
    }
    RegisterSchemaPrefix("/usr/local");
    RegisterSchemaPrefix("/usr");
}

struct GtkDialogEnvBootstrap {
    GtkDialogEnvBootstrap() noexcept { EnsureGtkDialogEnvironment(); }
} g_gtkDialogEnvBootstrap;

nfdresult_t EnsureGtkInitialized() {
    EnsureGtkDialogEnvironment();
    if (!gtk_init_check(nullptr, nullptr)) {
        NFDi_SetError("Failed to initialize GTK+ with gtk_init_check.");
        return NFD_ERROR;
    }

    // GtkFileChooserNative eagerly initializes a fallback GtkFileChooserDialog,
    // which aborts the process via g_error() if org.gtk.Settings.FileChooser is
    // absent. Verify the schema exists first so we never SIGABRT.
    GSettingsSchemaSource* src = g_settings_schema_source_get_default();
    GSettingsSchema* schema =
        src ? g_settings_schema_source_lookup(src, "org.gtk.Settings.FileChooser", TRUE) : nullptr;
    if (!schema) {
        NFDi_SetError("GTK GSettings schema 'org.gtk.Settings.FileChooser' is not installed.");
        return NFD_ERROR;
    }
    g_settings_schema_unref(schema);
    return NFD_OKAY;
}

char* AppendDefaultExtensionIfMissing(char* rawPath,
                                      GtkFileChooser* chooser,
                                      Pair_GtkFileFilter_FileExtension* map) {
    if (!rawPath || !*rawPath || !map || !map[0].filter) return rawPath;

    const std::filesystem::path chosen(rawPath);
    if (chosen.filename().has_extension()) return rawPath;

    const Pair_GtkFileFilter_FileExtension* selected = nullptr;
    GtkFileFilter* activeFilter = gtk_file_chooser_get_filter(chooser);
    if (activeFilter) {
        for (const Pair_GtkFileFilter_FileExtension* it = map; it->filter; ++it) {
            if (it->filter == activeFilter) {
                selected = it;
                break;
            }
        }
        // If activeFilter is the trailing "All files" wildcard filter, do not force an extension.
        if (!selected) return rawPath;
    } else {
        // Portal backends may not report the selected GtkFileFilter back; fall back to first filter.
        selected = &map[0];
    }

    if (!selected->extensionBegin || !selected->extensionEnd ||
        selected->extensionEnd <= selected->extensionBegin) {
        return rawPath;
    }

    std::string withExt(rawPath);
    withExt.push_back('.');
    withExt.append(selected->extensionBegin,
                   static_cast<size_t>(selected->extensionEnd - selected->extensionBegin));
    g_free(rawPath);
    return g_strdup(withExt.c_str());
}

}  // namespace

const char* NFD_GetError(void) {
    return g_errorstr;
}

void NFD_ClearError(void) {
    NFDi_SetError(nullptr);
}

/* public */

nfdresult_t NFD_Init(void) {
    return EnsureGtkInitialized();
}
void NFD_Quit(void) {
    // do nothing, GTK cannot be de-initialized
}

void NFD_FreePathN(nfdnchar_t* filePath) {
    assert(filePath);
    g_free(filePath);
}

void NFD_FreePathU8(nfdu8char_t* filePath) __attribute__((alias("NFD_FreePathN")));

nfdresult_t NFD_OpenDialogN(nfdnchar_t** outPath,
                            const nfdnfilteritem_t* filterList,
                            nfdfiltersize_t filterCount,
                            const nfdnchar_t* defaultPath) {
    nfdopendialognargs_t args{};
    args.filterList = filterList;
    args.filterCount = filterCount;
    args.defaultPath = defaultPath;
    return NFD_OpenDialogN_With_Impl(NFD_INTERFACE_VERSION, outPath, &args);
}

nfdresult_t NFD_OpenDialogN_With_Impl(nfdversion_t version,
                                      nfdnchar_t** outPath,
                                      const nfdopendialognargs_t* args) {
    (void)version;
    const nfdresult_t initRes = EnsureGtkInitialized();
    if (initRes != NFD_OKAY) return initRes;

    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Open File", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    if (!native) {
        NFDi_SetError("Failed to create GtkFileChooserNative.");
        return NFD_ERROR;
    }
    NativeDialog_Guard dialogGuard(native);
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(native), TRUE);

    AddFiltersToDialog(GTK_FILE_CHOOSER(native), args->filterList, args->filterCount);
    SetDefaultPath(GTK_FILE_CHOOSER(native), args->defaultPath);

    const gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (result == GTK_RESPONSE_ACCEPT) {
        *outPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (!*outPath) {
            NFDi_SetError("Selected file cannot be represented as a local filesystem path.");
            return NFD_ERROR;
        }
        return NFD_OKAY;
    }
    return NFD_CANCEL;
}

nfdresult_t NFD_OpenDialogU8(nfdu8char_t** outPath,
                             const nfdu8filteritem_t* filterList,
                             nfdfiltersize_t filterCount,
                             const nfdu8char_t* defaultPath)
    __attribute__((alias("NFD_OpenDialogN")));

nfdresult_t NFD_OpenDialogU8_With_Impl(nfdversion_t version,
                                       nfdu8char_t** outPath,
                                       const nfdopendialogu8args_t* args)
    __attribute__((alias("NFD_OpenDialogN_With_Impl")));

nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t** outPaths,
                                    const nfdnfilteritem_t* filterList,
                                    nfdfiltersize_t filterCount,
                                    const nfdnchar_t* defaultPath) {
    nfdopendialognargs_t args{};
    args.filterList = filterList;
    args.filterCount = filterCount;
    args.defaultPath = defaultPath;
    return NFD_OpenDialogMultipleN_With_Impl(NFD_INTERFACE_VERSION, outPaths, &args);
}

nfdresult_t NFD_OpenDialogMultipleN_With_Impl(nfdversion_t version,
                                              const nfdpathset_t** outPaths,
                                              const nfdopendialognargs_t* args) {
    (void)version;
    const nfdresult_t initRes = EnsureGtkInitialized();
    if (initRes != NFD_OKAY) return initRes;

    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Open Files", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    if (!native) {
        NFDi_SetError("Failed to create GtkFileChooserNative.");
        return NFD_ERROR;
    }
    NativeDialog_Guard dialogGuard(native);
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(native), TRUE);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(native), TRUE);

    AddFiltersToDialog(GTK_FILE_CHOOSER(native), args->filterList, args->filterCount);
    SetDefaultPath(GTK_FILE_CHOOSER(native), args->defaultPath);

    const gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (result == GTK_RESPONSE_ACCEPT) {
        GSList* fileList = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(native));
        if (!fileList) {
            NFDi_SetError("Selected files cannot be represented as local filesystem paths.");
            return NFD_ERROR;
        }
        *outPaths = static_cast<void*>(fileList);
        return NFD_OKAY;
    }
    return NFD_CANCEL;
}

nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t** outPaths,
                                     const nfdu8filteritem_t* filterList,
                                     nfdfiltersize_t filterCount,
                                     const nfdu8char_t* defaultPath)
    __attribute__((alias("NFD_OpenDialogMultipleN")));

nfdresult_t NFD_OpenDialogMultipleU8_With_Impl(nfdversion_t version,
                                               const nfdpathset_t** outPaths,
                                               const nfdopendialogu8args_t* args)
    __attribute__((alias("NFD_OpenDialogMultipleN_With_Impl")));

nfdresult_t NFD_SaveDialogN(nfdnchar_t** outPath,
                            const nfdnfilteritem_t* filterList,
                            nfdfiltersize_t filterCount,
                            const nfdnchar_t* defaultPath,
                            const nfdnchar_t* defaultName) {
    nfdsavedialognargs_t args{};
    args.filterList = filterList;
    args.filterCount = filterCount;
    args.defaultPath = defaultPath;
    args.defaultName = defaultName;
    return NFD_SaveDialogN_With_Impl(NFD_INTERFACE_VERSION, outPath, &args);
}

nfdresult_t NFD_SaveDialogN_With_Impl(nfdversion_t version,
                                      nfdnchar_t** outPath,
                                      const nfdsavedialognargs_t* args) {
    (void)version;
    const nfdresult_t initRes = EnsureGtkInitialized();
    if (initRes != NFD_OKAY) return initRes;

    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Save File", nullptr, GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    if (!native) {
        NFDi_SetError("Failed to create GtkFileChooserNative.");
        return NFD_ERROR;
    }
    NativeDialog_Guard dialogGuard(native);
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(native), TRUE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(native), TRUE);

    Pair_GtkFileFilter_FileExtension* filterMap =
        AddFiltersToDialogWithMap(GTK_FILE_CHOOSER(native), args->filterList, args->filterCount);
    Free_Guard<Pair_GtkFileFilter_FileExtension> filterMapGuard(filterMap);

    SetDefaultPath(GTK_FILE_CHOOSER(native), args->defaultPath);
    SetDefaultName(GTK_FILE_CHOOSER(native), args->defaultName);

    const gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (result == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (!filename) {
            NFDi_SetError("Selected save destination cannot be represented as a local path.");
            return NFD_ERROR;
        }
        *outPath = AppendDefaultExtensionIfMissing(filename, GTK_FILE_CHOOSER(native), filterMap);
        return NFD_OKAY;
    }
    return NFD_CANCEL;
}

nfdresult_t NFD_SaveDialogU8(nfdu8char_t** outPath,
                             const nfdu8filteritem_t* filterList,
                             nfdfiltersize_t filterCount,
                             const nfdu8char_t* defaultPath,
                             const nfdu8char_t* defaultName)
    __attribute__((alias("NFD_SaveDialogN")));

nfdresult_t NFD_SaveDialogU8_With_Impl(nfdversion_t version,
                                       nfdu8char_t** outPath,
                                       const nfdsavedialogu8args_t* args)
    __attribute__((alias("NFD_SaveDialogN_With_Impl")));

nfdresult_t NFD_PickFolderN(nfdnchar_t** outPath, const nfdnchar_t* defaultPath) {
    nfdpickfoldernargs_t args{};
    args.defaultPath = defaultPath;
    return NFD_PickFolderN_With_Impl(NFD_INTERFACE_VERSION, outPath, &args);
}

nfdresult_t NFD_PickFolderN_With_Impl(nfdversion_t version,
                                      nfdnchar_t** outPath,
                                      const nfdpickfoldernargs_t* args) {
    (void)version;
    const nfdresult_t initRes = EnsureGtkInitialized();
    if (initRes != NFD_OKAY) return initRes;

    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Select Folder", nullptr, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");
    if (!native) {
        NFDi_SetError("Failed to create GtkFileChooserNative.");
        return NFD_ERROR;
    }
    NativeDialog_Guard dialogGuard(native);
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(native), TRUE);

    SetDefaultPath(GTK_FILE_CHOOSER(native), args->defaultPath);

    const gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (result == GTK_RESPONSE_ACCEPT) {
        *outPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (!*outPath) {
            NFDi_SetError("Selected folder cannot be represented as a local filesystem path.");
            return NFD_ERROR;
        }
        return NFD_OKAY;
    }
    return NFD_CANCEL;
}

nfdresult_t NFD_PickFolderU8(nfdu8char_t** outPath, const nfdu8char_t* defaultPath)
    __attribute__((alias("NFD_PickFolderN")));

nfdresult_t NFD_PickFolderU8_With_Impl(nfdversion_t version,
                                       nfdu8char_t** outPath,
                                       const nfdpickfolderu8args_t* args)
    __attribute__((alias("NFD_PickFolderN_With_Impl")));

nfdresult_t NFD_PickFolderMultipleN(const nfdpathset_t** outPaths, const nfdnchar_t* defaultPath) {
    nfdpickfoldernargs_t args{};
    args.defaultPath = defaultPath;
    return NFD_PickFolderMultipleN_With_Impl(NFD_INTERFACE_VERSION, outPaths, &args);
}

nfdresult_t NFD_PickFolderMultipleN_With_Impl(nfdversion_t version,
                                              const nfdpathset_t** outPaths,
                                              const nfdpickfoldernargs_t* args) {
    (void)version;
    const nfdresult_t initRes = EnsureGtkInitialized();
    if (initRes != NFD_OKAY) return initRes;

    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Select Folders", nullptr, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");
    if (!native) {
        NFDi_SetError("Failed to create GtkFileChooserNative.");
        return NFD_ERROR;
    }
    NativeDialog_Guard dialogGuard(native);
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(native), TRUE);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(native), TRUE);

    SetDefaultPath(GTK_FILE_CHOOSER(native), args->defaultPath);

    const gint result = gtk_native_dialog_run(GTK_NATIVE_DIALOG(native));
    if (result == GTK_RESPONSE_ACCEPT) {
        GSList* fileList = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(native));
        if (!fileList) {
            NFDi_SetError("Selected folders cannot be represented as local filesystem paths.");
            return NFD_ERROR;
        }
        *outPaths = static_cast<void*>(fileList);
        return NFD_OKAY;
    }
    return NFD_CANCEL;
}

nfdresult_t NFD_PickFolderMultipleU8(const nfdpathset_t** outPaths, const nfdu8char_t* defaultPath)
    __attribute__((alias("NFD_PickFolderMultipleN")));

nfdresult_t NFD_PickFolderMultipleU8_With_Impl(nfdversion_t version,
                                               const nfdpathset_t** outPaths,
                                               const nfdpickfolderu8args_t* args)
    __attribute__((alias("NFD_PickFolderMultipleN_With_Impl")));

nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t* pathSet, nfdpathsetsize_t* count) {
    assert(pathSet);
    // const_cast because methods on GSList aren't const, but it should act
    // like const to the caller
    GSList* fileList = const_cast<GSList*>(static_cast<const GSList*>(pathSet));

    *count = g_slist_length(fileList);
    return NFD_OKAY;
}

nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t* pathSet,
                                 nfdpathsetsize_t index,
                                 nfdnchar_t** outPath) {
    assert(pathSet);
    // const_cast because methods on GSList aren't const, but it should act
    // like const to the caller
    GSList* fileList = const_cast<GSList*>(static_cast<const GSList*>(pathSet));

    // Note: this takes linear time... but should be good enough
    *outPath = static_cast<nfdnchar_t*>(g_slist_nth_data(fileList, index));

    return NFD_OKAY;
}

nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t* pathSet,
                                  nfdpathsetsize_t index,
                                  nfdu8char_t** outPath)
    __attribute__((alias("NFD_PathSet_GetPathN")));

void NFD_PathSet_FreePathN(const nfdnchar_t* filePath) {
    assert(filePath);
    (void)filePath;  // prevent warning in release build
    // no-op, because NFD_PathSet_Free does the freeing for us
}

void NFD_PathSet_FreePathU8(const nfdu8char_t* filePath)
    __attribute__((alias("NFD_PathSet_FreePathN")));

void NFD_PathSet_Free(const nfdpathset_t* pathSet) {
    assert(pathSet);
    // const_cast because methods on GSList aren't const, but it should act
    // like const to the caller
    GSList* fileList = const_cast<GSList*>(static_cast<const GSList*>(pathSet));

    // free all the nodes
    for (GSList* node = fileList; node; node = node->next) {
        assert(node->data);
        g_free(node->data);
    }

    // free the path set memory
    g_slist_free(fileList);
}

nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t* pathSet, nfdpathsetenum_t* outEnumerator) {
    // The pathset (GSList) is already a linked list, so the enumeration is itself
    outEnumerator->ptr = const_cast<void*>(pathSet);

    return NFD_OKAY;
}

void NFD_PathSet_FreeEnum(nfdpathsetenum_t*) {
    // Do nothing, because the enumeration is the pathset itself
}

nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t* enumerator, nfdnchar_t** outPath) {
    const GSList* fileList = static_cast<const GSList*>(enumerator->ptr);

    if (fileList) {
        *outPath = static_cast<nfdnchar_t*>(fileList->data);
        enumerator->ptr = static_cast<void*>(fileList->next);
    } else {
        *outPath = nullptr;
    }

    return NFD_OKAY;
}

nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t* enumerator, nfdu8char_t** outPath)
    __attribute__((alias("NFD_PathSet_EnumNextN")));
