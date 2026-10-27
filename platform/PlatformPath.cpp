#include "platform/PlatformPath.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#endif

namespace platform {

std::string GetExecutableDir()
{
#ifdef _WIN32
	char szPath[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, szPath, sizeof(szPath));
	if (n == 0 || n >= sizeof(szPath))
		return ".";
	char *pSlash = strrchr(szPath, '\\');
	if (pSlash == NULL)
		pSlash = strrchr(szPath, '/');
	if (pSlash)
		*pSlash = '\0';
	return std::string(szPath);
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(NULL, &size);
	std::vector<char> path(size);
	if (_NSGetExecutablePath(path.data(), &size) != 0)
		return ".";
	char *resolved = realpath(path.data(), NULL);
	std::string executable = resolved ? resolved : path.data();
	free(resolved);
	std::string::size_type slash = executable.find_last_of('/');
	return slash == std::string::npos ? "." : executable.substr(0, slash);
#else
	char szProcPath[100];
	char szProgramDir[256];
	snprintf(szProcPath, sizeof(szProcPath), "/proc/%d/exe", getpid());
	ssize_t n = readlink(szProcPath, szProgramDir, sizeof(szProgramDir) - 1);
	if (n == -1) {
		strcpy(szProgramDir, ".");
	} else {
		szProgramDir[n] = '\0';
		char *pSlash = strrchr(szProgramDir, '/');
		if (pSlash)
			*pSlash = '\0';
	}
	return std::string(szProgramDir);
#endif
}

std::string GetUserHomeDir()
{
#ifdef _WIN32
	const char *home = getenv("USERPROFILE");
	if (home && *home)
		return std::string(home);
	const char *drive = getenv("HOMEDRIVE");
	const char *path = getenv("HOMEPATH");
	if (drive && path)
		return std::string(drive) + std::string(path);
	return std::string(".");
#else
	const char *home = getenv("HOME");
	if (home && *home)
		return std::string(home);
	struct passwd *pw = getpwuid(getuid());
	if (pw && pw->pw_dir)
		return std::string(pw->pw_dir);
	return std::string(".");
#endif
}

bool EnsureDir(const std::string &strPath)
{
#ifdef _WIN32
	if (_access(strPath.c_str(), 0) == 0)
		return true;
	return _mkdir(strPath.c_str()) == 0;
#else
	struct stat st;
	if (stat(strPath.c_str(), &st) == 0)
		return S_ISDIR(st.st_mode);
	return mkdir(strPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH) == 0;
#endif
}

bool EnsureDirRecursive(const std::string &strPath)
{
	if (strPath.empty())
		return false;
	if (EnsureDir(strPath))
		return true;
	std::string::size_type pos = strPath.find_last_of("/\\");
	if (pos == std::string::npos || pos == 0)
		return false;
	std::string strParent = strPath.substr(0, pos);
	if (strParent.empty() || strParent == strPath)
		return false;
	if (!EnsureDirRecursive(strParent))
		return false;
	return EnsureDir(strPath);
}

std::string GetUserConfigDir()
{
#ifdef _WIN32
	const char *appdata = getenv("APPDATA");
	if (appdata && *appdata)
		return std::string(appdata) + "\\upgrade_tool";
	return GetUserHomeDir() + "\\.upgrade_tool";
#else
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		return std::string(xdg) + "/upgrade_tool";
	return GetUserHomeDir() + "/.config/upgrade_tool";
#endif
}

std::string GetUserLogDir()
{
#ifdef _WIN32
	return GetUserConfigDir() + "/log";
#else
	const char *xdg = getenv("XDG_STATE_HOME");
	if (xdg && *xdg)
		return std::string(xdg) + "/upgrade_tool/log";
	return GetUserHomeDir() + "/.local/state/upgrade_tool/log";
#endif
}

}
