#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/libxo'
  url 'https://github.com/Juniper/libxo/releases/download/1.0.0/libxo-1.0.0.tar.gz'
  sha1 'abf583b9c759077b3a9b8ece3c4f2bf1358937ce'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking", "--disable-silent-rules",
                          "--prefix=#{prefix}"
    system "make", "install"
  end
end