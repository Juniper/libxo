#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/libxo'
  url 'https://github.com/Juniper/libxo/releases/download/0.7.1/libxo-0.7.1.tar.gz'
  sha1 'fbc929b0716d989a8199cc0ed72a5a356c9ca8df'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking", "--disable-silent-rules",
                          "--prefix=#{prefix}"
    system "make", "install"
  end
end
