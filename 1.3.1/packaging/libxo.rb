#
# Homebrew formula file for libxo
# https://github.com/mxcl/homebrew
#

require 'formula'

class Libxo < Formula
  homepage 'https://github.com/Juniper/libxo'
  url 'https://github.com/Juniper/libxo/releases/download/1.3.1/libxo-1.3.1.tar.gz'
  sha1 'bfa8b00bb75fddf86b6203d4301f4317387998b2'

  depends_on 'libtool' => :build

  def install
    system "./configure", "--disable-dependency-tracking", "--disable-silent-rules",
                          "--prefix=#{prefix}"
    system "make", "install"
  end
end
