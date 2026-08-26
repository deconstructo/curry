class Libpiper < Formula
  desc "Native C/C++ library for Piper neural text-to-speech"
  homepage "https://github.com/OHF-Voice/piper1-gpl"
  license "GPL-3.0-only"

  url "https://github.com/OHF-Voice/piper1-gpl/archive/refs/tags/v1.7.0.tar.gz"
  sha256 "1fae26c92cd584671122157f3fd5d5500188906f370ad53e0f88e726ebe2e11d"
  version "1.7.0"

  # libpiper's own CMake build (libpiper/CMakeLists.txt) clones espeak-ng
  # via ExternalProject_Add and downloads a prebuilt onnxruntime release
  # archive, both during the build itself -- neither is pre-fetched as a
  # Homebrew `resource` before the sandboxed install phase, so this formula
  # needs network access during `install`, unlike almost everything else in
  # this tap.
  allow_network_access! :build

  depends_on "cmake" => :build
  depends_on "git"   => :build # ExternalProject_Add shells out to git directly

  def install
    args = std_cmake_args + %w[
      -DCMAKE_BUILD_TYPE=Release
      -DPIPER_BUILD_TESTS=OFF
    ]
    system "cmake", "-S", "libpiper", "-B", "build", *args
    system "cmake", "--build", "build", "-j", ENV.make_jobs.to_s
    system "cmake", "--install", "build"

    # libpiper.dylib and onnxruntime's dylib both ship with an @rpath-
    # relative LC_ID_DYLIB (confirmed via otool -D) instead of the absolute
    # install name every other Homebrew-packaged dylib curry links against
    # (openssl, libgit2, ...) -- that's why those never need an rpath, but
    # this does. Without this fix, anything that dlopen()s libpiper
    # transitively (curry's own (curry piper) module, loaded at runtime via
    # dlopen) fails with "no LC_RPATH's found": dyld has nothing to
    # substitute @rpath with, and modern macOS no longer falls back to
    # DYLD_FALLBACK_LIBRARY_PATH the way older dyld versions did. Rewriting
    # the id to the real, absolute Cellar path here means every consumer
    # resolves it exactly like any other Homebrew dylib -- no rpath
    # bookkeeping needed anywhere downstream.
    return unless OS.mac?

    onnxruntime_dylib = Dir.glob("#{lib}/libonnxruntime.*.dylib").first
    system "install_name_tool", "-id", "#{lib}/libpiper.dylib", "#{lib}/libpiper.dylib"
    system "install_name_tool", "-id", onnxruntime_dylib, onnxruntime_dylib if onnxruntime_dylib

    # -id above only fixes each dylib's own identity -- libpiper.dylib also
    # carries its own LC_LOAD_DYLIB *reference* to onnxruntime as
    # @rpath/libonnxruntime.<ver>.dylib (see otool -L), which -id does
    # nothing for. -change rewrites that specific reference to the same
    # absolute path onnxruntime's -id was just set to, so libpiper.dylib no
    # longer needs an rpath either.
    if onnxruntime_dylib
      onnxruntime_ref = Dir.glob("#{lib}/libpiper.dylib").first
      system "install_name_tool", "-change", "@rpath/#{File.basename(onnxruntime_dylib)}",
             onnxruntime_dylib, onnxruntime_ref
    end
  end

  test do
    assert_predicate include/"piper.h", :exist?
    next unless OS.mac?

    assert_predicate lib/"libpiper.dylib", :exist?
    id = shell_output("otool -D #{lib}/libpiper.dylib").lines.last.strip
    refute_match(/^@rpath/, id)
  end
end
