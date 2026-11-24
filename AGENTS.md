<!-- clank-right
languages   = ["cpp"]
conventions = "upstream"
-->

# This fork

This repository is a fork of upstream libtorrent (github.com/arvidn/libtorrent; the remote `upstream`). The fork's work is on `prime`: a commit of upstream's `RC_2_0` branch (3f5989b60db0 from 2026-10-05, 9 commits after the tag `v2.0.15`, as of 2026-11) plus the fork's own commits. `git log --oneline upstream/RC_2_0..prime` lists those commits, and each subject says what its commit changes. The fork's remote is `origin` (github.com/ludios/libtorrent); push to it only when the user asks.

The user's machines get libtorrent from the user's nixpkgs fork in `~/code/system/nixpkgs`. Its `pkgs/by-name/li/libtorrent-rasterbar-2_0_x/package.nix` fetches one commit of `prime` from github.com/ludios/libtorrent (by `rev` and `hash`), and the user's qBittorrent fork (`~/code/system/qbittorrent`) is built against that package. A new commit on `prime` therefore changes nothing on the user's machines until it is pushed to `origin` and that `package.nix` names it. Keep `prime` on upstream's 2.0 series (`RC_2_0`): libtorrent 2.1 (upstream's `master`) removed `storage_params::mapped_files`, which this qBittorrent uses.

To move `prime` to a newer upstream commit, rebase it onto that commit of `upstream/RC_2_0` (the rules in "Using git safely" apply).

Keep each of the fork's changes small and in a commit of its own, so that `prime` rebases with few conflicts. Upstream has no style guide, so follow the style of the code around each change (tabs for indentation, `TORRENT_ASSERT`, a `TORRENT_TEST` in `test/` for each behavior change), and change as few of upstream's lines as you can: each changed line can conflict in a later rebase. For the same reason, don't add entries to `ChangeLog`, whose top changes with each upstream release.

# Before committing

Build libtorrent and its tests with the debug checks of upstream's CI (assertions, full invariant checks, and libstdc++'s checked iterators), in the build environment of the system's nixpkgs package, and run the tests:

	nix-shell '<nixpkgs>' -A libtorrent-rasterbar-2_0_x --run '
		cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug -Dbuild_tests=ON \
			-Ddeveloper-options=ON -Dasserts=on -Dinvariant-checks=full -Ddebug-iterators=ON &&
		cmake --build cmake-build-debug --parallel "$(nproc)" &&
		cd cmake-build-debug && ctest -j8 --timeout 600'

`<nixpkgs>` is the nixpkgs that the system was built from (`/var/nixpkgs`, through `NIX_PATH`), so nix finds every dependency already built. Don't use `nix-shell ~/code/system/nixpkgs` instead: that checkout is often ahead of the system.

Then run upstream's simulations (`simulation/`: tests of swarms in a simulated network, which only b2 builds), as upstream's CI does:

	cd simulation
	nix-shell -p boost boost-build --run 'b2 -j"$(nproc)" debug-iterators=on invariant-checks=full asserts=on picker-debugging=on'

b2 prints `**passed**` for each test that passes, and exits with status 0 only if every test passed.

On xclank in 2026-11, while other work was loading it, the CMake build took 3 minutes from an empty `cmake-build-debug/`, `ctest` took 1.5 minutes, and the simulations took 3 minutes from an empty `simulation/bin/`. Both builds put their output in directories that `.gitignore` lists (`cmake-build-debug/`, and b2's `bin/` directories).

Each test binary in `cmake-build-debug/test/` lists its test cases with `-l`, and when given arguments, runs only the test cases that they name. Name each case exactly as `-l` prints it: the absolute path of the source file, then `.` and the case's name (for example, `./test_resume ~/code/system/libtorrent/test/test_resume.cpp.all_pieces_alerts`).

Don't add compiler warnings: upstream's CI builds with `warnings-as-errors=on`. As of 2026-11, the CMake build already prints 3 warnings (`-Warray-bounds` twice and `-Wstringop-overflow` once).

The build and every test must pass, except for 2 of `ctest`'s 107 tests, which fail as of 2026-11 without any change of yours (every simulation passes):

- `test_torrent_info`: its test case `sanitize_path_trailing_dots` still expects trailing dots to stay in file names on Linux; the fork's commit c30b682b2730 makes Linux remove them, as Windows does.
- `test_upnp`: its UPnP search over multicast gets no answer on the user's machines, with and without the fork's commits.
