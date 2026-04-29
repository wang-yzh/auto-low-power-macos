class AutoLowPowerMacos < Formula
  desc "Event-driven Low Power Mode automation for macOS laptops"
  homepage "https://github.com/wang-yzh/auto-low-power-macos"
  url "https://github.com/wang-yzh/auto-low-power-macos/releases/download/v0.1.1/auto-low-power-macos-v0.1.1.tar.gz"
  sha256 "643b11ad7e0ee4a1ddea7f1736f5e36db1e2c1a830ae0f03b64235aac9a24865"
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
    assert_predicate libexec/"src/auto_low_power_listener.c", :exist?
    assert_predicate bin/"auto-low-power-install", :exist?
    assert_predicate bin/"auto-low-power-uninstall", :exist?
  end
end
