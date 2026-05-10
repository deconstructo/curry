class CurryFull < Formula
  desc "R7RS Scheme interpreter — full build with Qt6, PLplot, SymEngine, Neo4j, GraphQL"
  homepage "https://github.com/deconstructo/curry"
  license "GPL-3.0-only"

  url "https://github.com/deconstructo/curry/archive/refs/tags/v0.7.2.tar.gz"
  sha256 "a753424947d30d4087aa122a323fce6452850a5d780ce101e33459a8151cfda6"
  version "0.7.2"

  head "https://github.com/deconstructo/curry.git", branch: "main"

  depends_on "cmake"      => :build
  depends_on "pkg-config" => :build

  # Required
  depends_on "bdw-gc"
  depends_on "gmp"

  # REPL history / line editing
  depends_on "readline"

  # Always-on optional modules
  depends_on "sqlite"
  depends_on "openssl@3"
  depends_on "libgit2"
  depends_on "libpng"
  depends_on "jpeg-turbo"

  # Full-build extras
  depends_on "qt@6"
  depends_on "plplot"
  depends_on "symengine"

  conflicts_with "deconstructo/curry/curry",
    :because => "both install the 'curry' binary"

  def install
    qt6 = Formula["qt@6"].opt_prefix
    ssl = Formula["openssl@3"].opt_prefix

    args = std_cmake_args + %W[
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
      -DBUILD_MODULE_CRYPTO=ON
      -DBUILD_MODULE_LDAP=ON
      -DBUILD_MODULE_STORAGE=ON
      -DBUILD_MODULE_IMAGE=ON
      -DBUILD_MODULE_GIT=ON
      -DBUILD_MODULE_GRAPHQL=ON
      -DBUILD_MODULE_QT6=ON
      -DBUILD_MODULE_PLPLOT=ON
      -DBUILD_MODULE_NEO4J=ON
      -DBUILD_MODULE_SYMENGINE=ON
      -DBUILD_MODULE_VECDB=OFF
      -DCMAKE_PREFIX_PATH=#{ssl};#{qt6}
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
    # Smoke-test Qt6 and PLplot modules loaded (no display needed)
    assert_match "curry", shell_output("#{bin}/curry -v")
  end
end
