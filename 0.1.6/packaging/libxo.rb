#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/@PACKAGE-NAME@'
  url 'https://github.com/Juniper/libxo/releases/0.1.6/libxo-0.1.6.tar.gz'
  sha1 'dc9c6616c7b1364356ec7f90f6440fcb617f68e0'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking",
                          "--prefix=#{prefix}"
    system "make install"
  end
end
