#include "platform/PlatformPath.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool WriteFile(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return false;
    fputs("test file\n", f);
    return fclose(f) == 0;
}

#ifdef __APPLE__
static bool CheckExecutable(const std::string &executable, const std::string &expected)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl(executable.c_str(), executable.c_str(), "--expect-executable", expected.c_str(),
              static_cast<char *>(NULL));
        _exit(127);
    }
    int status = 0;
    return pid > 0 && waitpid(pid, &status, 0) == pid &&
           WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--expect-executable")
        return platform::GetExecutableDir() == argv[2] ? 0 : 1;
    char temp[] = "/tmp/upgrade_tool_path_XXXXXX";
    char *root = mkdtemp(temp);
    if (!root)
        return 1;
    char *resolved = realpath(root, NULL);
    if (!resolved)
        return 1;
    std::string base(resolved);
    free(resolved);
    std::string executable = platform::GetExecutableDir();
    if (executable.empty() || executable == "." || executable[0] != '/')
        return 1;
    if (chdir(root) != 0 || platform::GetExecutableDir() != executable)
        return 1;
    setenv("XDG_CONFIG_HOME", base.c_str(), 1);
    setenv("XDG_STATE_HOME", base.c_str(), 1);
    std::string dir = base + "/upgrade_tool";
    if (platform::GetUserConfigDir() != dir ||
        platform::GetUserLogDir() != dir + "/log" ||
        !platform::EnsureDirRecursive(dir + "/nested/child"))
        return 1;
    std::string config = dir + "/file";
    if (!WriteFile(config) || platform::EnsureDir(config))
        return 1;
#ifdef __APPLE__
    // Run a copy from a bundle with a Unicode/space path and an unrelated cwd.
    std::string contents = base + "/测试 App.app/Contents";
    std::string macos = contents + "/MacOS";
    std::string resources = contents + "/Resources";
    if (!platform::EnsureDirRecursive(macos) || !platform::EnsureDirRecursive(resources))
        return 1;
    std::string copy = macos + "/path_test";
    FILE *in = fopen((executable + "/test_platform_path").c_str(), "rb");
    FILE *out = fopen(copy.c_str(), "wb");
    if (!in || !out)
        return 1;
    char buffer[8192];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), in)) != 0)
        if (fwrite(buffer, 1, count, out) != count)
            return 1;
    fclose(in);
    fclose(out);
    chmod(copy.c_str(), 0700);
    if (!CheckExecutable(copy, macos))
        return 1;
    remove(copy.c_str());
    rmdir(macos.c_str());
    rmdir(resources.c_str());
    rmdir(contents.c_str());
    rmdir((base + "/测试 App.app").c_str());
#endif
    remove(config.c_str());
    rmdir((dir + "/nested/child").c_str());
    rmdir((dir + "/nested").c_str());
    rmdir(dir.c_str());
    chdir("/");
    rmdir(root);
    puts("test_platform_path: PASS");
    return 0;
}
