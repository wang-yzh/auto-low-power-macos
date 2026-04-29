class AutoLowPowerMacos < Formula
  desc "Event-driven Low Power Mode automation for macOS laptops"
  homepage "https://github.com/wang-yzh/auto-low-power-macos"
  url "https://github.com/wang-yzh/auto-low-power-macos/releases/download/v0.1.0/auto-low-power-macos-v0.1.0.tar.gz"
  sha256 "72fcf198d6a27df953cf966d8af653bebe6d1d69220b94497d330c4019bbd47f"
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
