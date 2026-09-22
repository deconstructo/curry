class XeusZmq < Formula
  desc "ZeroMQ-based transport layer for the xeus Jupyter kernel protocol"
  homepage "https://github.com/jupyter-xeus/xeus-zmq"
  url "https://github.com/jupyter-xeus/xeus-zmq/archive/refs/tags/4.0.0.tar.gz"
  sha256 "6a87c0edeefae1350743071cd78c825d6ab21e771b52610ef7e41c434af20db6"
  version "4.0.0"
  license "BSD-3-Clause"

  depends_on "cmake" => :build
  depends_on "cppzmq"
  depends_on "deconstructo/curry/xeus"
  depends_on "nlohmann-json"
  depends_on "openssl@3"
  depends_on "zeromq"

  def install
    prefix_paths = [
      Formula["deconstructo/curry/xeus"].opt_prefix,
      Formula["nlohmann-json"].opt_prefix,
      Formula["cppzmq"].opt_prefix,
      Formula["zeromq"].opt_prefix,
      Formula["openssl@3"].opt_prefix,
    ]

    args = std_cmake_args + %W[
      -DXEUS_ZMQ_BUILD_TESTS=OFF
      -DXEUS_ZMQ_BUILD_WITHOUT_LIBSODIUM=ON
      -DCMAKE_PREFIX_PATH=#{prefix_paths.join(";")}
    ]
    system "cmake", "-B", "build", *args
    system "cmake", "--build", "build", "-j", ENV.make_jobs.to_s
    system "cmake", "--install", "build"
  end

  test do
    assert_predicate include/"xeus-zmq/xeus-zmq.hpp", :exist?
    assert_predicate lib/"libxeus-zmq.dylib", :exist?
  end
end
