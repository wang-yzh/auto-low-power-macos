class AutoLowPowerMacos < Formula
  desc "Event-driven Low Power Mode automation for macOS laptops"
  homepage "https://github.com/wang-yzh/auto-low-power-macos"
  url "https://github.com/wang-yzh/auto-low-power-macos/releases/download/v0.1.1/auto-low-power-macos-v0.1.1.tar.gz"
  sha256 "7046233858346aaa2597f41c772f6391306cd015d69bb899d5d91d2b88deba3b"
  license "MIT"

  depends_on :macos

  def install
    libexec.install Dir["*"]

    (bin/"auto-low-power-install").write_env_script libexec/"scripts/install.sh",
      AUTO_LOW_POWER_ROOT_DIR: libexec

    (bin/"auto-low-power-uninstall").write_env_script libexec/"scripts/uninstall.sh",
      AUTO_LOW_POWER_ROOT_DIR: libexec
  end

  test do
    assert_path_exists libexec/"src/auto_low_power_listener.c"
    assert_path_exists bin/"auto-low-power-install"
    assert_path_exists bin/"auto-low-power-uninstall"
  end
end
