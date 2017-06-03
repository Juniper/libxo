#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/libxo'
  url 'https://github.com/Juniper/libxo/releases/download/0.8.0/libxo-0.8.0.tar.gz'
  sha1 'eafc0bdedd9e30e70949e0dd1502fc46818f6b2f'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking", "--disable-silent-rules",
                          "--prefix=#{prefix}"
    system "make", "install"
  end
end
