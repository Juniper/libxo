#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/libxo'
  url 'https://github.com/Juniper/libxo/releases/download/1.5.0/libxo-1.5.0.tar.gz'
  sha1 'd09438dfbf937184368bbb4636bd0527035ba7d4'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking", "--disable-silent-rules",
                          "--prefix=#{prefix}"
    system "make", "install"
  end
end
