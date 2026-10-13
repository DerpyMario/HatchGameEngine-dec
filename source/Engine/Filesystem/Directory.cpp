#include <Engine/Diagnostics/Memory.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/Path.h>
#include <Engine/Utilities/StringUtils.h>

#if WIN32 || XBOX
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

bool CompareFunction(std::filesystem::path i, std::filesystem::path j) {
	std::string pathA = Path::ToString(i);
	std::string pathB = Path::ToString(j);

	return pathA.compare(pathB) < 0;
}

bool Directory::Exists(const char* path) {
#if WIN32 || XBOX
	DWORD ftyp = GetFileAttributesA(path);
	if (ftyp == INVALID_FILE_ATTRIBUTES) {
		return false; // Something is wrong with your path
	}
	if (ftyp & FILE_ATTRIBUTE_DIRECTORY) {
		return true;
	}
#else
	struct stat pathStat;
	if (stat(path, &pathStat) == 0) {
		return S_ISDIR(pathStat.st_mode);
	}
#endif
	return false;
}
bool Directory::Create(const char* path) {
	if (path[0] == '\0') {
		return false;
	}

#if WIN32 || XBOX
	BOOL result = CreateDirectoryA(path, NULL);
	if (result == FALSE) {
		DWORD err = GetLastError();
		if (err == ERROR_ALREADY_EXISTS) {
			return true;
		}

		return false;
	}
#else
	int result = mkdir(path, 0700);
	if (result != 0) {
		if (errno == EEXIST) {
			return true;
		}

		return false;
	}
#endif

	return true;
}

// Resource paths are all resolved relative to the working directory, so opening
// a different project from the editor means moving into its folder.
bool Directory::SetCurrentWorkingDirectory(const char* path) {
#if WIN32 || XBOX
	return _chdir(path) == 0;
#else
	return chdir(path) == 0;
#endif
}

// Creates every missing directory along the path, like "mkdir -p". Unlike
// Path::Create, the last component is a directory too, whether or not the path
// ends in a separator.
bool Directory::CreatePath(const char* path) {
	char partial[MAX_PATH_LENGTH];
	size_t length = strlen(path);
	if (length == 0 || length >= sizeof(partial)) {
		return false;
	}

	memcpy(partial, path, length + 1);

	for (size_t i = 1; i <= length; i++) {
		bool end = i == length;
		if (!end && partial[i] != '/' && partial[i] != '\\') {
			continue;
		}

		char separator = partial[i];
		partial[i] = '\0';

		if (!Directory::Exists(partial) && !Directory::Create(partial)) {
			return false;
		}

		if (!end) {
			partial[i] = separator;
		}
	}

	return true;
}

void Directory::GetFiles(std::vector<std::filesystem::path>* files,
	const char* path,
	const char* searchPattern,
	SearchOptions options) {
	char fullpath[MAX_PATH_LENGTH];

#if WIN32 || XBOX
	char winPath[MAX_PATH_LENGTH];
	snprintf(winPath, MAX_PATH_LENGTH, "%s%s*", path, path[strlen(path) - 1] == '/' ? "" : "/");

	WIN32_FIND_DATA data;
	HANDLE hFind = FindFirstFile(winPath, &data);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (data.cFileName[0] == '.' && data.cFileName[1] == 0) {
				continue;
			}
			if (data.cFileName[0] == '.' && data.cFileName[1] == '.' &&
				data.cFileName[2] == 0) {
				continue;
			}

			bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			bool isValidPath = false;

			if (isDirectory) {
				if (options.AllDirs) {
					snprintf(fullpath,
						sizeof fullpath,
						"%s/%s",
						path,
						data.cFileName);
					Directory::GetFiles(files, fullpath, searchPattern, true);
				}
				else if (options.ListDirs) {
					isValidPath = true;
				}
			}
			else if (StringUtils::WildcardMatch(data.cFileName, searchPattern)) {
				isValidPath = true;
			}

			if (isValidPath) {
				std::string entryName = std::string(data.cFileName);

				std::filesystem::path pathFs =
					std::filesystem::path(std::string(path));
				std::filesystem::path entryFs = std::filesystem::path(entryName);

				std::string tempPath = Path::ToString(pathFs / entryFs);
				std::replace(tempPath.begin(), tempPath.end(), '\\', '/');

				if (isDirectory) {
					tempPath += "/";
				}

				std::filesystem::path finalPath = std::filesystem::u8path(tempPath);

				files->push_back(finalPath);
			}
		} while (FindNextFile(hFind, &data));
		FindClose(hFind);
	}
#else
	DIR* dir = opendir(path);
	if (dir) {
		struct dirent* d;

		while ((d = readdir(dir)) != NULL) {
			if (d->d_name[0] == '.' && !d->d_name[1]) {
				continue;
			}
			if (d->d_name[0] == '.' && d->d_name[1] == '.' && !d->d_name[2]) {
				continue;
			}

			bool isDirectory = d->d_type == DT_DIR || d->d_type == DT_LNK;
			bool isValidPath = false;

			if (isDirectory) {
				if (options.AllDirs) {
					snprintf(fullpath,
						sizeof fullpath,
						"%s/%s",
						path,
						d->d_name);
					Directory::GetFiles(files, fullpath, searchPattern, true);
				}
				else if (options.ListDirs) {
					isValidPath = true;
				}
			}
			else if (StringUtils::WildcardMatch(d->d_name, searchPattern)) {
				isValidPath = true;
			}

			if (isValidPath) {
				std::string entryName = std::string(d->d_name);

				std::filesystem::path pathFs =
					std::filesystem::u8path(std::string(path));
				std::filesystem::path entryFs = std::filesystem::u8path(entryName);

				std::string tempPath = Path::ToString(pathFs / entryFs);
				std::replace(tempPath.begin(), tempPath.end(), '\\', '/');

				if (isDirectory) {
					tempPath += "/";
				}

				std::filesystem::path finalPath = std::filesystem::u8path(tempPath);

				files->push_back(finalPath);
			}
		}
		closedir(dir);
	}
#endif
}

void Directory::GetFiles(std::vector<std::filesystem::path>* files,
	const char* path,
	const char* searchPattern,
	bool allDirs) {
	SearchOptions options;
	options.AllDirs = allDirs;
	GetFiles(files, path, searchPattern, options);
}

void Directory::GetDirectories(std::vector<std::filesystem::path>* files,
	const char* path,
	const char* searchPattern,
	bool allDirs) {
#if WIN32 || XBOX
	char winPath[MAX_PATH_LENGTH];
	snprintf(winPath, MAX_PATH_LENGTH, "%s%s*", path, path[strlen(path) - 1] == '/' ? "" : "/");

	WIN32_FIND_DATA data;
	HANDLE hFind = FindFirstFile(winPath, &data);

	int i;
	char fullpath[MAX_PATH_LENGTH];
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (data.cFileName[0] == '.' && !data.cFileName[1]) {
				continue;
			}
			if (data.cFileName[0] == '.' && data.cFileName[1] == '.' &&
				!data.cFileName[2]) {
				continue;
			}

			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (StringUtils::WildcardMatch(data.cFileName, searchPattern)) {
					std::string entryName = std::string(data.cFileName);

					std::filesystem::path pathFs =
						std::filesystem::path(std::string(path));
					std::filesystem::path entryFs =
						std::filesystem::path(entryName);

					std::string tempPath = Path::ToString(pathFs / entryFs);
					std::replace(tempPath.begin(), tempPath.end(), '\\', '/');

					std::filesystem::path finalPath =
						std::filesystem::u8path(tempPath);

					files->push_back(finalPath);
				}
				if (allDirs) {
					snprintf(fullpath,
						sizeof fullpath,
						"%s/%s",
						path,
						data.cFileName);
					Directory::GetFiles(files, fullpath, searchPattern, true);
				}
			}
		} while (FindNextFile(hFind, &data));
		FindClose(hFind);
	}
#else
	char fullpath[MAX_PATH_LENGTH];
	DIR* dir = opendir(path);
	if (dir) {
		int i;
		struct dirent* d;

		while ((d = readdir(dir)) != NULL) {
			if (d->d_name[0] == '.' && !d->d_name[1]) {
				continue;
			}
			if (d->d_name[0] == '.' && d->d_name[1] == '.' && !d->d_name[2]) {
				continue;
			}

			if (d->d_type == DT_DIR) {
				if (StringUtils::WildcardMatch(d->d_name, searchPattern)) {
					std::string entryName = std::string(d->d_name);

					std::filesystem::path pathFs =
						std::filesystem::u8path(std::string(path));
					std::filesystem::path entryFs =
						std::filesystem::u8path(entryName);

					files->push_back(pathFs / entryFs);
				}
				if (allDirs) {
					snprintf(fullpath,
						sizeof fullpath,
						"%s/%s",
						path,
						d->d_name);
					Directory::GetFiles(files, fullpath, searchPattern, true);
				}
			}
		}
		closedir(dir);
	}
#endif
}
std::vector<std::filesystem::path>
Directory::GetDirectories(const char* path, const char* searchPattern, bool allDirs) {
	std::vector<std::filesystem::path> files;
	Directory::GetDirectories(&files, path, searchPattern, allDirs);
	return files;
}

bool Directory::IsEmpty(const char* path) {
	std::filesystem::path pathFs = std::filesystem::u8path(path);
	std::filesystem::file_status status = std::filesystem::status(pathFs);

	if (std::filesystem::is_directory(status)) {
		std::error_code err;
		return std::filesystem::is_empty(pathFs, err);
	}

	return false;
}

void Directory::SortEntries(std::vector<std::filesystem::path>* files) {
	if (files == nullptr || files->size() < 2) {
		return;
	}

	std::sort(files->begin(), files->end(), CompareFunction);
}
