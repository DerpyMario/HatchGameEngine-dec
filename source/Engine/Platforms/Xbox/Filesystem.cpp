#ifdef XBOX

// nxdk's C++ library declares std::filesystem but is built without the part
// that implements it, and the engine leans on it for its paths. These are the
// pieces the engine reaches, written to the C++17 rules for a path with '/' as
// its separator -- which is the grammar nxdk's library declares -- and to the
// Windows API calls nxdk gives the Xbox for anything that has to look at the
// disk.

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace {

// A path taken apart into what the comparison and normalisation rules work on:
// whether it starts at the root, its filenames, and whether it ends with a
// separator (which makes its last element an empty filename).
struct PathParts {
	bool HasRoot = false;
	std::vector<std::string> Names;
	bool TrailingSeparator = false;
};

PathParts SplitPath(std::string_view text) {
	PathParts parts;

	size_t at = 0;
	if (!text.empty() && text[0] == '/') {
		parts.HasRoot = true;
		while (at < text.size() && text[at] == '/') {
			at++;
		}
	}

	while (at < text.size()) {
		size_t end = text.find('/', at);
		if (end == std::string_view::npos) {
			end = text.size();
		}

		parts.Names.push_back(std::string(text.substr(at, end - at)));

		at = end;
		if (at < text.size()) {
			while (at < text.size() && text[at] == '/') {
				at++;
			}
			if (at == text.size()) {
				parts.TrailingSeparator = true;
			}
		}
	}

	return parts;
}

void ClearError(error_code* ec) {
	if (ec) {
		ec->clear();
	}
}

} // namespace

path::__string_view path::__root_directory() const {
	if (!__pn_.empty() && __pn_[0] == '/') {
		return __string_view(__pn_.data(), 1);
	}

	return __string_view();
}

// The last element, unless the path ends in a separator, in which case that
// element is an empty filename.
path::__string_view path::__filename() const {
	if (__pn_.empty() || __pn_.back() == '/') {
		return __string_view();
	}

	size_t slash = __pn_.find_last_of('/');
	if (slash == string_type::npos) {
		return __string_view(__pn_);
	}

	return __string_view(__pn_.data() + slash + 1, __pn_.size() - slash - 1);
}

// Element by element: a path with a root directory sorts after one without,
// and then the filenames are compared in order.
int path::__compare(__string_view other) const {
	PathParts a = SplitPath(__string_view(__pn_));
	PathParts b = SplitPath(other);

	if (a.HasRoot != b.HasRoot) {
		return a.HasRoot ? 1 : -1;
	}

	if (a.TrailingSeparator) {
		a.Names.push_back("");
	}
	if (b.TrailingSeparator) {
		b.Names.push_back("");
	}

	size_t count = a.Names.size() < b.Names.size() ? a.Names.size() : b.Names.size();
	for (size_t i = 0; i < count; i++) {
		int result = a.Names[i].compare(b.Names[i]);
		if (result != 0) {
			return result;
		}
	}

	if (a.Names.size() == b.Names.size()) {
		return 0;
	}

	return a.Names.size() < b.Names.size() ? -1 : 1;
}

// Separators collapse, "." goes, a name followed by ".." goes with it, ".."
// directly under the root goes, and nothing at all becomes ".".
path path::lexically_normal() const {
	if (__pn_.empty()) {
		return path();
	}

	PathParts parts = SplitPath(__string_view(__pn_));

	std::vector<std::string> names;
	bool trailing = parts.TrailingSeparator;

	for (size_t i = 0; i < parts.Names.size(); i++) {
		const std::string& name = parts.Names[i];
		bool last = i + 1 == parts.Names.size();

		if (name == ".") {
			if (last) {
				trailing = true;
			}
			continue;
		}

		if (name == "..") {
			if (!names.empty() && names.back() != "..") {
				names.pop_back();
				if (last) {
					trailing = true;
				}
				continue;
			}

			if (parts.HasRoot) {
				continue;
			}
		}

		names.push_back(name);
		if (last) {
			trailing = parts.TrailingSeparator;
		}
	}

	if (!names.empty() && names.back() == "..") {
		trailing = false;
	}

	std::string result = parts.HasRoot ? "/" : "";
	for (size_t i = 0; i < names.size(); i++) {
		if (i > 0) {
			result += "/";
		}
		result += names[i];
	}

	if (trailing && !names.empty()) {
		result += "/";
	}

	if (result.empty()) {
		result = ".";
	}

	return path(result);
}

// A title runs from the drive it was launched off; a path that names no drive
// of its own is on that one.
path __absolute(const path& p, error_code* ec) {
	ClearError(ec);

	const std::string& text = p.native();
	if ((text.size() >= 2 && text[1] == ':') || (!text.empty() && text[0] == '/')) {
		return p;
	}

	char workingDirectory[MAX_PATH];
	if (!_getcwd(workingDirectory, sizeof workingDirectory)) {
		return p;
	}

	std::string base = workingDirectory;
	if (!base.empty() && base.back() != '\\' && base.back() != '/') {
		base += "\\";
	}

	return path(base + text);
}

file_status __status(const path& p, error_code* ec) {
	ClearError(ec);

	DWORD attributes = GetFileAttributesA(p.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		return file_status(file_type::not_found);
	}

	if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
		return file_status(file_type::directory);
	}

	return file_status(file_type::regular);
}

// A folder with nothing in it but "." and "..", or a file with no bytes.
bool __fs_is_empty(const path& p, error_code* ec) {
	ClearError(ec);

	DWORD attributes = GetFileAttributesA(p.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		if (ec) {
			*ec = std::make_error_code(std::errc::no_such_file_or_directory);
		}
		return false;
	}

	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
		WIN32_FILE_ATTRIBUTE_DATA data;
		if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &data)) {
			return false;
		}

		return data.nFileSizeHigh == 0 && data.nFileSizeLow == 0;
	}

	std::string pattern = p.native();
	if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/') {
		pattern += "\\";
	}
	pattern += "*";

	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA(pattern.c_str(), &data);
	if (find == INVALID_HANDLE_VALUE) {
		return true;
	}

	bool empty = true;
	do {
		const char* name = data.cFileName;
		if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
			empty = false;
			break;
		}
	} while (FindNextFileA(find, &data));

	FindClose(find);

	return empty;
}

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif /* XBOX */
