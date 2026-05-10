class Curry < Formula
  desc "R7RS Scheme interpreter with actor concurrency and extended numerics"
  homepage "https://github.com/deconstructo/curry"
  license "GPL-3.0-only"

  # Update url + sha256 after tagging a release:
  #   git tag v0.7.2 && git push origin v0.7.2
  #   curl -L https://github.com/deconstructo/curry/archive/refs/tags/v0.7.2.tar.gz | shasum -a 256
  url "https://github.com/deconstructo/curry/archive/refs/tags/v0.7.2.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  version "0.7.2"

  head "https://github.com/deconstructo/curry.git", branch: "main"

  depends_on "cmake"     => :build
  depends_on "pkg-config" => :build

  # Required
  depends_on "bdw-gc"
  depends_on "gmp"

  # REPL history / line editing
  depends_on "readline"

  # Always-on optional modules with brew-available deps
  depends_on "sqlite"
  depends_on "openssl@3"
  depends_on "libgit2"
  depends_on "libpng"
  depends_on "jpeg-turbo"
  # curl ships with macOS; no separate dep needed

  def install
    args = std_cmake_args + %W[
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_MODULE_CRYPTO=ON
      -DBUILD_MODULE_LDAP=ON
      -DBUILD_MODULE_STORAGE=ON
      -DBUILD_MODULE_IMAGE=ON
      -DBUILD_MODULE_GIT=ON
      -DBUILD_MODULE_GRAPHQL=OFF
      -DBUILD_MODULE_QT6=OFF
      -DBUILD_MODULE_PLPLOT=OFF
      -DBUILD_MODULE_NEO4J=OFF
      -DBUILD_MODULE_SYMENGINE=OFF
      -DBUILD_MODULE_VECDB=OFF
      -DCMAKE_PREFIX_PATH=#{Formula["openssl@3"].opt_prefix}
    ]

    system "cmake", "-B", "build", *args
    system "cmake", "--build", "build", "-j", ENV.make_jobs.to_s
    system "cmake", "--install", "build"
  end

  test do
    assert_equal "3", shell_output("#{bin}/curry -e '(display (+ 1 2)) (newline)'").chomp
    assert_equal "120",
      shell_output("#{bin}/curry -e '(define (fact n) (if (= n 0) 1 (* n (fact (- n 1))))) " \
                   "(display (fact 5)) (newline)'").chomp
  end
end
