class Curry < Formula
  desc "R7RS Scheme interpreter with actor concurrency and extended numerics"
  homepage "https://github.com/deconstructo/curry"
  license "GPL-3.0-only"

  # Update url + sha256 after tagging a release:
  #   git tag v1.2.6 && git push origin v1.2.6
  #   curl -L https://github.com/deconstructo/curry/archive/refs/tags/v1.6.2.tar.gz | shasum -a 256
  url "https://github.com/deconstructo/curry/archive/refs/tags/v1.10.2.tar.gz"
  sha256 "e61bc9f216b97ad8712e490f13b46b1a40e8b75295e1336565b5a255678ab594"
  version "1.10.2"

  head "https://github.com/deconstructo/curry.git", branch: "main"

  option "with-llvm",      "Build LLVM ORC JIT backend (tiered native compilation)"
  option "with-ffi",       "Build general C FFI (libffi backend)"
  option "with-mpfr",      "Build MPFR arbitrary-precision float support"
  option "with-qt6",       "Build Qt6 GUI module"
  option "with-plplot",    "Build PLplot scientific plotting module"
  option "with-ldap",      "Build LDAP/LDAPS module"

  depends_on "cmake"      => :build
  depends_on "pkg-config" => :build

  # Required
  depends_on "bdw-gc"
  depends_on "gmp"
  depends_on "readline"

  # Always-on modules
  depends_on "openssl@3"
  depends_on "sqlite"
  depends_on "libgit2"
  depends_on "libpng"
  depends_on "jpeg-turbo"
  depends_on "libpaho-mqtt"
  # curl ships with macOS; no separate dep needed for graphql/storage
  #
  # (curry hdf5) is pure Scheme + FFI — it dlopen's libhdf5 at runtime rather
  # than linking at build time, so it's deliberately not a depends_on here
  # (Homebrew's dependency types are all build-time gates, with no "runtime
  # recommends" concept the way apt/dnf's Recommends: fields have). Users who
  # want (curry hdf5) should separately `brew install hdf5`.

  # Option-gated deps
  depends_on "openldap" if build.with? "ldap"
  depends_on "llvm"     if build.with? "llvm"
  depends_on "libffi"   if build.with? "ffi"
  depends_on "mpfr"     if build.with? "mpfr"
  depends_on "qt@6"     if build.with? "qt6"
  depends_on "plplot"   if build.with? "plplot"

  def install
    prefix_paths = [
      Formula["openssl@3"].opt_prefix,
      Formula["readline"].opt_prefix,
      Formula["libpaho-mqtt"].opt_prefix,
    ]
    prefix_paths << Formula["openldap"].opt_prefix  if build.with? "ldap"
    prefix_paths << Formula["llvm"].opt_prefix      if build.with? "llvm"
    prefix_paths << Formula["libffi"].opt_prefix    if build.with? "ffi"
    prefix_paths << Formula["mpfr"].opt_prefix      if build.with? "mpfr"
    prefix_paths << Formula["qt@6"].opt_prefix      if build.with? "qt6"
    prefix_paths << Formula["plplot"].opt_prefix    if build.with? "plplot"

    args = std_cmake_args + %W[
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_PREFIX_PATH=#{prefix_paths.join(";")}
      -DBUILD_MODULE_CRYPTO=ON
      -DBUILD_MODULE_LDAP=#{build.with?("ldap") ? "ON" : "OFF"}
      -DBUILD_MODULE_STORAGE=ON
      -DBUILD_MODULE_GRAPHQL=ON
      -DBUILD_MODULE_REDIS=ON
      -DBUILD_MODULE_MQTT=ON
      -DBUILD_MODULE_IMAGE=ON
      -DBUILD_MODULE_GIT=ON
      -DBUILD_MODULE_MCP=ON
      -DBUILD_MODULE_PROFILING=ON
      -DBUILD_LLVM=#{build.with?("llvm")   ? "ON" : "OFF"}
      -DBUILD_FFI=#{build.with?("ffi")     ? "ON" : "OFF"}
      -DBUILD_MPFR=#{build.with?("mpfr")   ? "ON" : "OFF"}
      -DBUILD_MODULE_QT6=#{build.with?("qt6")      ? "ON" : "OFF"}
      -DBUILD_MODULE_PLPLOT=#{build.with?("plplot") ? "ON" : "OFF"}
      -DBUILD_MODULE_NEO4J=ON
      -DBUILD_MODULE_VECDB=OFF
    ]

    system "cmake", "-B", "build", *args
    system "cmake", "--build", "build", "-j", ENV.make_jobs.to_s
    system "cmake", "--install", "build"
    doc.install Dir["docs/*"]
  end

  test do
    assert_equal "3", shell_output("#{bin}/curry -e '(display (+ 1 2)) (newline)'").chomp
    assert_equal "120",
      shell_output("#{bin}/curry -e '(define (fact n) (if (= n 0) 1 (* n (fact (- n 1))))) " \
                   "(display (fact 5)) (newline)'").chomp
    assert_equal "#t", shell_output("#{bin}/curry -e '(display (prime? 17)) (newline)'").chomp
    assert_equal "(2 2 3)", shell_output("#{bin}/curry -e '(display (factor 12)) (newline)'").chomp
    if build.with? "mpfr"
      assert_equal "#t",
        shell_output("#{bin}/curry -e '(display (mpfr? (with-precision 256 (mpfr-pi)))) (newline)'").chomp
    end
  end
end
