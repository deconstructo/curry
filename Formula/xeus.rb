class Xeus < Formula
  desc "C++ implementation of the Jupyter kernel protocol"
  homepage "https://github.com/jupyter-xeus/xeus"
  url "https://github.com/jupyter-xeus/xeus/archive/refs/tags/6.0.6.tar.gz"
  sha256 "2cf58d5466a541f7acf668e3500507ca54eb2393780ba3743d94308c24d6ca80"
  version "6.0.6"
  license "BSD-3-Clause"

  depends_on "cmake" => :build
  depends_on "nlohmann-json"

  def install
    args = std_cmake_args + %W[
      -DXEUS_BUILD_TESTS=OFF
      -DXEUS_BUILD_SHARED_LIBS=ON
      -DXEUS_BUILD_STATIC_LIBS=OFF
      -DCMAKE_PREFIX_PATH=#{Formula["nlohmann-json"].opt_prefix}
    ]
    system "cmake", "-B", "build", *args
    system "cmake", "--build", "build", "-j", ENV.make_jobs.to_s
    system "cmake", "--install", "build"
  end

  test do
    assert_predicate include/"xeus/xeus.hpp", :exist?
    assert_predicate lib/"libxeus.dylib", :exist?
  end
end
