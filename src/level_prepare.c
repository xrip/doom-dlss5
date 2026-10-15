#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "level_prepare.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#endif

static char *join_path(const char *directory, const char *name) {
    size_t size = strlen(directory) + strlen(name) + 2;
    char *path = allocate(size, 1);
    snprintf(path, size, "%s/%s", directory, name);
    return path;
}

static bool path_exists(const char *path, bool directory) {
    struct stat info;
    if (stat(path, &info)) return false;
#ifdef _WIN32
    bool is_directory = (info.st_mode & _S_IFMT) == _S_IFDIR;
#else
    bool is_directory = S_ISDIR(info.st_mode);
#endif
    return is_directory == directory;
}

static char *executable_directory(void) {
    char path[32768];
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path)) fail("Cannot resolve executable path");
#else
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0 || (size_t)length >= sizeof(path) - 1) fail("Cannot resolve executable path");
    path[length] = 0;
#endif
    char *separator = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (backslash && (!separator || backslash > separator)) separator = backslash;
    if (!separator) fail("Executable path has no directory");
    *separator = 0;
    char *result = allocate(strlen(path) + 1, 1);
    strcpy(result, path);
    return result;
}

static bool voxel_source_exists(const char *root) {
    char *models = join_path(root, "voxels"), *first = join_path(root, "VOXELDEF1.txt"),
         *second = join_path(root, "voxeldef.txt");
    bool exists = path_exists(models, true) && path_exists(first, false) && path_exists(second, false);
    free(models); free(first); free(second);
    return exists;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    for (size_t index = 0; index < size; ++index) hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t hash_file(uint64_t hash, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) fail("Cannot read cache input %s", path);
    uint8_t bytes[65536];
    size_t size;
    while ((size = fread(bytes, 1, sizeof(bytes), file)) != 0) hash = hash_bytes(hash, bytes, size);
    if (ferror(file)) fail("Cannot hash cache input %s", path);
    fclose(file);
    return hash;
}

static uint64_t hash_models(const char *root) {
    uint64_t hash = 0;
#ifdef _WIN32
    char *pattern = join_path(root, "voxels/*.kvx");
    WIN32_FIND_DATAA file;
    HANDLE search = FindFirstFileA(pattern, &file);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return 0;
        fail("Cannot list voxel cache inputs");
    }
    do {
        if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        uint64_t entry = hash_bytes(UINT64_C(14695981039346656037), file.cFileName, strlen(file.cFileName));
        entry = hash_bytes(entry, &file.nFileSizeHigh, sizeof(file.nFileSizeHigh));
        entry = hash_bytes(entry, &file.nFileSizeLow, sizeof(file.nFileSizeLow));
        entry = hash_bytes(entry, &file.ftLastWriteTime, sizeof(file.ftLastWriteTime));
        hash ^= entry;
    } while (FindNextFileA(search, &file));
    DWORD status = GetLastError();
    FindClose(search);
    if (status != ERROR_NO_MORE_FILES) fail("Cannot finish listing voxel cache inputs");
#else
    char *directory = join_path(root, "voxels");
    DIR *search = opendir(directory);
    if (!search) fail("Cannot list voxel cache inputs");
    struct dirent *file;
    while ((file = readdir(search)) != NULL) {
        const char *extension = strrchr(file->d_name, '.');
        if (!extension || strcmp(extension, ".kvx")) continue;
        char *path = join_path(directory, file->d_name);
        struct stat info;
        if (stat(path, &info)) fail("Cannot stat voxel cache input %s", path);
        free(path);
        uint64_t entry = hash_bytes(UINT64_C(14695981039346656037), file->d_name, strlen(file->d_name));
        entry = hash_bytes(entry, &info.st_size, sizeof(info.st_size));
        entry = hash_bytes(entry, &info.st_mtim, sizeof(info.st_mtim));
        hash ^= entry;
    }
    closedir(search);
    free(directory);
#endif
    return hash;
}

static void make_directory(const char *path) {
#ifdef _WIN32
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0777);
#endif
    if (result && (errno != EEXIST || !path_exists(path, true))) fail("Cannot create cache directory %s", path);
}

static int run_converter(const char *const *arguments) {
#ifdef _WIN32
    size_t size = 1;
    for (size_t index = 0; arguments[index]; ++index) size += strlen(arguments[index]) + 3;
    char *command = allocate(size, 1);
    size_t offset = 0;
    for (size_t index = 0; arguments[index]; ++index) {
        if (strchr(arguments[index], '"')) fail("Invalid quote in converter argument");
        offset += (size_t)snprintf(command + offset, size - offset, "\"%s\" ", arguments[index]);
    }
    STARTUPINFOA startup = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = GetStdHandle(STD_INPUT_HANDLE), .hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE),
        .hStdError = GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION process = {0};
    BOOL started = CreateProcessA(arguments[0], command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
        NULL, NULL, &startup, &process);
    free(command);
    if (!started) fail("Cannot start wad-convert (Windows error %lu)", GetLastError());
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0) fail("Cannot wait for wad-convert");
    DWORD status;
    if (!GetExitCodeProcess(process.hProcess, &status)) fail("Cannot read wad-convert result");
    CloseHandle(process.hProcess);
    return (int)status;
#else
    pid_t process = fork();
    if (process < 0) fail("Cannot start wad-convert");
    if (!process) { execv(arguments[0], (char *const *)arguments); _exit(127); }
    int status;
    while (waitpid(process, &status, 0) < 0)
        if (errno != EINTR) fail("Cannot wait for wad-convert");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

bool level_is_wad(const char *path) {
    const char *extension = strrchr(path, '.');
    return extension && strlen(extension) == 4 && tolower((unsigned char)extension[1]) == 'w' &&
        tolower((unsigned char)extension[2]) == 'a' && tolower((unsigned char)extension[3]) == 'd';
}

LevelSource level_source_create(const char *wad, int voxel_mode) {
    LevelSource source = {.wad = wad};
    char *directory = executable_directory();
#ifdef _WIN32
    source.converter = join_path(directory, "wad-convert.exe");
#else
    source.converter = join_path(directory, "wad-convert");
#endif
    if (!path_exists(source.converter, false)) fail("wad-convert must be beside the viewer: %s", source.converter);
    if (voxel_mode != VOXELS_OFF) {
        const char *locations[] = {".", "..", "../.."};
        for (size_t index = 0; index < sizeof(locations) / sizeof(locations[0]); ++index) {
            char *root = join_path(directory, locations[index]);
            if (voxel_source_exists(root)) { source.voxel_root = root; break; }
            free(root);
        }
        if (!source.voxel_root && voxel_mode == VOXELS_ON)
            fail("Voxel source not found: need voxels/, VOXELDEF1.txt and voxeldef.txt beside the viewer or in its parent folders");
    }
    uint64_t hash = hash_file(UINT64_C(14695981039346656037), source.wad);
    hash = hash_file(hash, source.converter);
    bool voxels = source.voxel_root != NULL;
    hash = hash_bytes(hash, &voxels, sizeof(voxels));
    if (voxels) {
        char *first = join_path(source.voxel_root, "VOXELDEF1.txt"), *second = join_path(source.voxel_root, "voxeldef.txt");
        hash = hash_file(hash, first); hash = hash_file(hash, second);
        free(first); free(second);
        uint64_t models = hash_models(source.voxel_root);
        hash = hash_bytes(hash, &models, sizeof(models));
    }
    char key[17];
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)hash);
    char *cache = join_path(directory, "cache");
    make_directory(cache);
    source.directory = join_path(cache, key);
    make_directory(source.directory);
    free(cache);
    printf("Cache: %s\n", source.directory);
    free(directory);
    return source;
}

char *level_source_prepare(const LevelSource *source, const char *map) {
    char name[6];
    if (strlen(map) != 5) fail("Expected MAP01..MAP32");
    for (unsigned index = 0; index < 5; ++index) name[index] = (char)toupper((unsigned char)map[index]);
    name[5] = 0;
    if (strncmp(name, "MAP", 3) || name[3] < '0' || name[3] > '3' || name[4] < '0' || name[4] > '9' ||
        atoi(name + 3) < 1 || atoi(name + 3) > 32) fail("Expected MAP01..MAP32");
    char filename[16];
    snprintf(filename, sizeof(filename), "%s.drmap", name);
    char *output = join_path(source->directory, filename);
    char *stamp = scene_sidecar(output, ".ready"), *music = scene_sidecar(output, ".mid"),
         *voxel = scene_sidecar(output, ".drvox");
    FILE *ready = fopen(stamp, "rb");
    if (ready) {
        int flags = fgetc(ready), tail = fgetc(ready);
        fclose(ready);
        if ((flags == 0 || flags == 1) && tail == EOF && path_exists(output, false) &&
            (!(flags & 1) || path_exists(music, false)) && (!source->voxel_root || path_exists(voxel, false))) {
            printf("Using cached %s\n", name);
            free(stamp); free(music); free(voxel);
            return output;
        }
    }
    const char *arguments[11] = {source->converter, source->wad, output, name, NULL};
    char *models = NULL, *first = NULL, *second = NULL;
    if (source->voxel_root) {
        models = join_path(source->voxel_root, "voxels");
        first = join_path(source->voxel_root, "VOXELDEF1.txt");
        second = join_path(source->voxel_root, "voxeldef.txt");
        arguments[4] = "--voxels"; arguments[5] = models;
        arguments[6] = "--voxeldef"; arguments[7] = first;
        arguments[8] = "--voxeldef"; arguments[9] = second;
    }
    printf("Preparing %s from %s (%s)\n", name, source->wad, source->voxel_root ? "voxels" : "sprites");
    int status = run_converter(arguments);
    free(models); free(first); free(second);
    if (status) fail("wad-convert failed for %s with exit code %d", name, status);
    if (!path_exists(output, false) || (source->voxel_root && !path_exists(voxel, false)))
        fail("wad-convert did not produce the prepared level");
    uint8_t flags = path_exists(music, false) ? 1 : 0;
    write_file(stamp, &flags, 1);
    free(stamp); free(music); free(voxel);
    return output;
}

void level_source_free(LevelSource *source) {
    free(source->converter); free(source->directory); free(source->voxel_root);
    *source = (LevelSource){0};
}
