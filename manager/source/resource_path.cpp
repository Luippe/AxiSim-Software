#include "resource_path.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <cstdint>
#include <vector>

namespace {

	std::filesystem::path findExecutableDir() {
	#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (len == 0 || len >= MAX_PATH) {
			return std::filesystem::current_path();
		}
		return std::filesystem::path(buffer).parent_path();
	#elif defined(__APPLE__)
		std::uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::vector<char> buffer(size);
		if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
			return std::filesystem::current_path();
		}
		return std::filesystem::weakly_canonical(buffer.data()).parent_path();
	#elif defined(__linux__)
		std::vector<char> buffer(1024);
		for (;;) {
			const ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size());
			if (len < 0) {
				return std::filesystem::current_path();
			}
			if (static_cast<size_t>(len) < buffer.size()) {
				return std::filesystem::path(
					std::string(buffer.data(), static_cast<size_t>(len))
				).parent_path();
			}
			buffer.resize(buffer.size() * 2);
		}
	#else
		return std::filesystem::current_path();
	#endif
	}

}

namespace Resources {

	const std::filesystem::path& executableDir() {
		static const std::filesystem::path dir = findExecutableDir();
		return dir;
	}

	std::string resolve(std::string_view relative) {

		std::error_code ec;

		// packaged layout: assets/ and graphics/shaders/ sit next to the exe
		const std::filesystem::path beside = executableDir() / relative;
		if (std::filesystem::exists(beside, ec)) {
			return beside.string();
		}

	#ifdef AXISIM_SOURCE_DIR
		// build-directory exe: read straight out of the repo it was built from.
		// The path is baked in at configure time and simply won't exist on a
		// machine the package was copied to, so the check above is what runs there.
		const std::filesystem::path inSourceTree =
			std::filesystem::path(AXISIM_SOURCE_DIR) / relative;
		if (std::filesystem::exists(inSourceTree, ec)) {
			return inSourceTree.string();
		}
	#endif

		return std::string(relative);
	}

}
