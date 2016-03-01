#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/libxo'
  url 'https://github.com/Juniper/libxo/releases/download/0.4.7/libxo-0.4.7.tar.gz'
  sha1 'ffcb87f051e3dd05cbc63b381f733b2fe95e191c'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking",
                          "--prefix=#{prefix}"
    system "make", "install"
  end
end
